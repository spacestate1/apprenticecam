/* nc_post.c - the LinuxCNC post: writes a whole part as G-code in the format of the user's own Mastercam post
 * (LinuxCNC 4020 router).
 *
 * Where the moves come from:
 *   - Mastercam's operations: the binary NCI stored in the part (exactly what Mastercam computed);
 *   - operations made or edited here: their generated moves.
 * Port of ../tools/mcam_post.py (validated: BRACKET4 matches Mastercam's posted program block for block); rules in
 * ../formats/post.md. This is not an MP (.pst) interpreter.
 *
 * How it works: each operation is first turned into a list of events (Ev: tool change, rapid, feed, arc, drill point,
 * drill end); the whole list is checked (nothing absurd or unsupported may reach the machine); then the events are
 * written out as numbered blocks.
 *
 * Entry points (nc_post.h):
 *   nc_post_part        post a part to a FILE (the G-code program)
 *   nc_motions          the same events as a motion list, for Backplot / Verify (formats/backplot.md)
 *   nc_motion_point     a point along one of those motions
 *   nc_extents          Stock Setup "NCI extents": the box of the toolpaths
 *   nc_drill_text       the drill page's post text labels
 *   nc_cutter_comp_on, nc_drill_cycles_on, nc_coolant_on, nc_force_tc_on: the MCAM_* environment switches
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "platform.h"
#include "nc_post.h"
#include "cam_postvals.h"
#include "cam_fileop.h"

/* ------------------------------------------------------------------------------------------------ constants */

#define MM_PER_INCH 25.4

/* NCI record codes read here (the first i16 of a binary NCI record) */
enum {
    NCI_OP_START = -2,          /* start of an op's section (+0x1c: the op id) */
    NCI_OP_END = -3,            /* end of an op's section */
    NCI_DRILL_END = 80,         /* end of a drill cycle */
    NCI_DRILL_FIRST = 81,       /* drill point: the first of a cycle */
    NCI_DRILL_NEXT = 100,       /* drill point: each further one */
    NCI_TOOL_FIRST = 1000,      /* tool change records 1000-1002 */
    NCI_TOOL_LAST = 1002,
    NCI_TOOL_NAME = 20001,      /* tool name text */
    NCI_MATERIAL = 20016,       /* material text */
};
#define NCI_REC_MAX 0x6e0       /* larger records end the read (the section is taken as damaged) */
#define NCI_REC_BUF 0x371       /* record buffer: one byte more than copied, so texts always end */

/* cutter compensation codes on a move (Move.cc / the NCI cc field) */
enum { CC_LEFT = 41, CC_RIGHT = 42, CC_OFF = 140 };

/* the op's compensation type (decisions batch dec2 (Q29)) */
enum { COMP_COMPUTER = 0, COMP_CONTROL = 1, COMP_WEAR = 2, COMP_REVERSE_WEAR = 3, COMP_OFF = 4 };

/* Mastercam drill cycles (NCI 81 +0x18 / CamParams.drill_cycle) */
enum {
    CYC_DRILL = 0,              /* Drill/Counterbore: G81 (G82 with a dwell) */
    CYC_PECK = 1,               /* Peck Drill: G83 */
    CYC_CHIP_BREAK = 2,         /* Chip Break: G73 */
    CYC_TAP = 3,
    CYC_BORE1 = 4,
    CYC_BORE2 = 5,
    CYC_MISC1 = 6,              /* Fine Bore (shift) */
    CYC_MISC2 = 7,              /* Rigid Tapping */
};

/* the V9-style coolant word (record 0x398 = PV_COOL_OLD, TlCoolant.OldStyle) */
enum { COOLW_OFF = 8, COOLW_FLOOD = 0x10, COOLW_MIST = 0x20, COOLW_THRU = 0x40 };

/* X-style coolant entry positions */
enum { COOL_BEFORE = 0, COOL_WITH = 1, COOL_AFTER = 2 };

/* Mastercam NCI arc planes */
enum { PLANE_XY = 0, PLANE_YZ = 1, PLANE_XZ = 2 };

#define MAX_TOOLS 256           /* tool list in the program header */
#define TOOL_NAME_LEN 80
#define COORD_LIMIT 1e6         /* larger coordinates / feeds are refused as invalid */

/* ------------------------------------------------------------------------------------------------ events */
enum { EV_RAPID = 0, EV_FEED = 1, EV_CW = 2, EV_CCW = 3, EV_DRILL_END = 80, EV_DRILL = 81, EV_TOOL = 1000 };

/* One event of the program: a tool change, a rapid / feed move, an arc, a drill point or a drill cycle end. */
typedef struct {
    int kind;                /* EV_* */
    double p[3];             /* end point (world) / drill point (x, y, depth) / tool change: first position */
    double c[3];             /* arc centre (world; the depth axis is unused) */
    int plane;               /* arcs: 0 XY, 1 YZ, 2 XZ (Mastercam NCI planes) */
    double feed, rplane, peck;
    int cyc;                 /* drill: Mastercam cycle (1 Peck Drill G83, 2 Chip Break G73) */
    double dwell;            /* drill: NCI 81 +0x38 (bnci_wr_81: per-point override or record 0x1250) /
                                CamParams.drill_dwell (batch 8) */
    int tool, rpm, op;
    int fitted;              /* arcs refitted from our generated moves (endpoints only within tolerance of the
                                circle) */
    int geo;                 /* batch 10: an arc of generated moves - kind is its direction in the plane's (u, v) frame;
                                posted XZ arcs take the NCI / G18 direction, the reverse (mccore FUN_180944830 /
                                FUN_18094ce10 flip plane 2) */
    int has_s;               /* NCI 81: start point (+0x88) and R distance (+0x68 - +0x70) - backplot expansion */
    double s[3], rdist;
    int cc, ccst, dnum;      /* cutter compensation code on this move (41 / 42 start, 140 end; Move.cc / the NCI cc
                                field), from a stored NCI (ccst), the D word (dnum: the tool number, dec2 Q30) */
} Ev;

/* The op's compensation type, decisions batch dec2 (Q29): 0 Computer, 1 Control, 2 Wear, 3 Reverse wear, 4 Off.
   Read from: Contour record 0x5d0 (CamParams.ct_comp), the Pocket finish pass 0x13a8 (cam2d_pock_fin.c), the other
   types 0x5d0 (CamParams.mc_comp). */
static int op_cc_type(const CamParams *p) {
    if (p->type == CAM_CONTOUR)
        return p->ct_comp;
    if (p->type == CAM_POCKET) {
        int16_t v = 0;
        if (p->pk_ok)
            memcpy(&v, p->pk_p + (0x13a8 - 0x1228), 2);   /* pk_p = record 0x1228.. */
        return v;
    }
    return p->mc_comp;
}

/* 1 when the environment variable NAME is set to exactly VALUE. */
static int env_is(const char *name, const char *value) {
    const char *e = getenv(name);
    return e && !strcmp(e, value);
}

/* The environment switches (nc_post.h): each is on only when the variable is exactly "1". */
int nc_cutter_comp_on(void) {
    return env_is("MCAM_CUTTER_COMP", "1");
}
int nc_drill_cycles_on(void) {
    return env_is("MCAM_DRILL_CYCLES", "1");
}
int nc_coolant_on(void) {
    return env_is("MCAM_COOLANT", "1");
}
int nc_force_tc_on(void) {
    return env_is("MCAM_FORCE_TOOL_CHANGE", "1");
}

/* Upper-cases S in place (the program's comments are upper case). */
static void upcase(char *s) {
    for (char *q = s; *q; q++)
        *q = (char)toupper((unsigned char)*q);
}

/* ------------------------------------------------------------------------------------------------ op lookup */

/* The stored op with id OPID (the last one if the id repeats); NULL when there is none. */
static const Op *stored_op_by_id(const McamFile *f, int opid) {
    const Op *so = NULL;
    for (int k = 0; k < f->nops; k++)
        if (f->ops[k].id == opid)
            so = &f->ops[k];
    return so;
}

/* The op made / edited here that stands for the F.ops entry with id OPID (the last one if several); NULL when there
   is none. */
static const CamOp *camop_by_id(const McamFile *f, CamOp **camops, int ncamops, int opid) {
    const CamOp *mine = NULL;
    for (int j = 0; j < ncamops; j++)
        if (camops[j]->op_index >= 0 && camops[j]->op_index < f->nops && f->ops[camops[j]->op_index].id == opid)
            mine = camops[j];
    return mine;
}

/* The op record to read (*N = its size): the stored op's record (SO), or for an op made here (MINE, unless it is a
   stored op regenerated here) its defaults op's record. NULL when there is none. */
static const uint8_t *op_record(const McamFile *f, const Op *so, const CamOp *mine, uint32_t *n) {
    const uint8_t *r = NULL;
    *n = 0;
    if (so && so->raw) {
        r = so->raw;
        *n = so->raw_n;
    }
    if (mine && !(so && so->raw && mine->p.file_op_id > 0))
        r = cam_default_op_record(mine->p.type, mcam_is_metric(f), n);   /* an op made here: its defaults op */
    return r;
}

/* ------------------------------------------------------------------------------------------------ coolant
   (batch 11, MCAM_COOLANT=1)

   What Mastercam passes (cam_postvals.c) depends on the machine's UseCoolantFromPost:
   - X style (UseCoolantFromPost off - the user's BRACKET4 / A001 / ... machines): coolant entries in the op's canned
     text array (record 0xb52, NCI 1025 at the section start): option k on / off, before / with / after;
   - V9 style (UseCoolantFromPost on - test-cube-10mm's): the op's coolant word (record 0x398, NCI 1000-1002 +0x48:
     0x10 flood, 0x20 mist, 0x40 thru, 8 off).

   How the stock post the parts name (Generic Fanuc 3X Mill.pst, shared Mcam2017 mill/Posts) writes them:
   - X style:
       pcan l.1404 ("before": own blocks ahead of "T M6" in ptlchg$ l.736, ahead of the spindle in ptlchg0$);
       pcan1_cool l.1461 via pscool l.1003 ("with": on the "G43 H Z" line, ptlchg$ l.753; own block in ptlchg0$ l.716);
       pcan2 l.1486 ("after": own blocks after the tool change, pcom_movea l.966);
       pcant_out l.1614 (an on code is suppressed while that coolant is on; an off code is written only while some
       coolant is on);
       pretract l.770 (at each tool change / end: the off code of every coolant still on, before "M5");
       codes scool50 / 51 = M8 / M9 (coolant 1, Flood), scool52 / 53 = M7 / M9 (coolant 2, Mist) l.407-410 -
       LinuxCNC's M7 mist / M8 flood / M9 off.
   - V9 style: pscool's scoolant on the "G43" line (sm08* l.395-398), M9 at pretract's "G91 G28 Z0." line.
   Thru-tool (M88 / M89) and custom options 4-10 as the stock post (2026-11-04; refused before); V9 thru = sm08_2 "M8";
   V9 mist is written M7 (Q13) (the stock V9 table writes M8 for every option); V9 M9 only after an op that turned
   coolant on; non-coolant canned text is not posted (as before). */

/* One op's coolant, as op_coolant reads it. */
typedef struct {
    int n;                   /* X style: the entries in e[] */
    int v9;                  /* V9 style: 0 none, 1 flood, 2 mist, 3 thru */
    struct {
        int8_t k;            /* coolant option - 1 (0 = coolant 1 Flood, 1 = coolant 2 Mist, 2 = thru-tool, ...) */
        int8_t on;           /* 1 on, 0 off */
        int8_t pos;          /* COOL_BEFORE / COOL_WITH / COOL_AFTER */
    } e[20];
} OpCool;

/* A 16-bit record field at OFF (0 when the record R of N bytes is too short), with the op's patches over it. */
static int16_t rec16(const uint8_t *r, uint32_t n, const CamParams *p, int off) {
    int16_t v = 0;
    if (r && (uint32_t)off + 2 <= n)
        memcpy(&v, r + off, 2);
    if (p)
        for (int k = 0; k < p->nrpatch; k++)
            if (p->rpatch[k].off == off && p->rpatch[k].n == 2)
                memcpy(&v, p->rpatch[k].v, 2);
    return v;
}

/* Fills C with the coolant of the op with id OPID (see the section comment above). */
static void op_coolant(const McamFile *f, CamOp **camops, int ncamops, int opid, OpCool *c) {
    memset(c, 0, sizeof *c);
    const Op *so = stored_op_by_id(f, opid);                     /* the op as stored in the part */
    const CamOp *mine = camop_by_id(f, camops, ncamops, opid);   /* the op as made / edited here (if it is one) */
    uint32_t n;
    const uint8_t *r = op_record(f, so, mine, &n);
    const CamParams *p = mine ? &mine->p : NULL;

    if (pv_cool_frompost(pv_machine(f))) {   /* V9 style: the op's coolant word */
        int16_t w = rec16(r, n, NULL, PV_COOL_OLD), patched = 0;
        if (p)
            for (int k = 0; k < p->nrpatch; k++)
                if (p->rpatch[k].off == PV_COOL_OLD && p->rpatch[k].n == 2) {
                    memcpy(&w, p->rpatch[k].v, 2);
                    patched = 1;
                }
        if (p && !patched) {   /* mcam_saveops.c: the op's tool block +0x50 (TlCoolant.OldStyle), 16 / 8 by
                                  CamParams.coolant */
            const Tool *tl = mcam_tool(f, p->tool_num);
            int st = tl ? tl->coolant_style : 0;
            w = p->coolant                                   ? COOLW_FLOOD
              : (st == COOLW_MIST || st == COOLW_THRU)        ? st
              : (mine->p.file_op_id > 0 && w != COOLW_FLOOD) ? w
              :                                                COOLW_OFF;
        }
        c->v9 = w == COOLW_FLOOD ? 1 : w == COOLW_MIST ? 2 : w == COOLW_THRU ? 3 : 0;
        return;
    }

    /* X style: the coolant entries of the canned text array */
    int on = r && n > PV_CAN_ON ? r[PV_CAN_ON] : 0;
    if (p)
        for (int k = 0; k < p->nrpatch; k++)
            if (p->rpatch[k].off == PV_CAN_ON && p->rpatch[k].n == 1)
                on = p->rpatch[k].v[0];
    if (!on)
        return;                                         /* bnci_wr_1025(op, op[0xae8] = 0): no entries */
    for (int i = 0; i < 20; i++) {
        int v = rec16(r, n, p, PV_CAN + 2 * i);         /* entry: code -101..-120, +/- 1000 x (position + 3) */
        int rm = v % 1000;
        int a = v < 0 ? -v : v;
        if (!v)
            continue;
        if (rm > -101 || rm < -120)
            continue;                                   /* canned text (not coolant): not posted */
        int code = -rm;
        c->e[c->n].k = (int8_t)((code - 101) / 2);
        c->e[c->n].on = (code - 101) % 2 == 0;
        c->e[c->n].pos = (int8_t)(a / 1000 - 3);
        c->n++;
    }
}

/* ------------------------------------------------------------------------------------------------ force tool change
   batch 12 (MCAM_FORCE_TOOL_CHANGE=1): the Tool page's Force tool change 25196 = op +0x289 = record 0x2f1
   (ToolPageBase::MapToOp 0x180886e20; an op made here: its record patch, tp_apply). Mastercam's Post pre-pass
   AssignToolChanges (mcmill FUN_180037ca0, ported as viewer/mp/nci_ascii.c assign_tool_changes; formats/mp_engine.md
   "CBnciPost pre-pass") gives an op that starts with the previous op's tool number gcode 1002 (a full tool change)
   when its +0x289 is set (and the op is not op_is_notool / wire), else 1000 (null tool change); the stock post's
   ptlchg$ then writes the retract + "T M6" block as for another tool. */
#define REC_FORCE_TC 0x2f1

/* 1 when the op with id OPID has Force tool change set. */
static int op_force_tc(const McamFile *f, CamOp **camops, int ncamops, int opid) {
    const Op *so = stored_op_by_id(f, opid);
    const CamOp *mine = camop_by_id(f, camops, ncamops, opid);
    uint32_t n;
    const uint8_t *r = op_record(f, so, mine, &n);   /* as op_coolant */
    int v = r && n > REC_FORCE_TC ? r[REC_FORCE_TC] : 0;
    if (mine)
        for (int k = 0; k < mine->p.nrpatch; k++)
            if (mine->p.rpatch[k].off == REC_FORCE_TC && mine->p.rpatch[k].n == 1)
                v = mine->p.rpatch[k].v[0];
    return v != 0;
}

/* The X-style codes of coolant option K + 1 (ON: on / off code); NULL for an unknown option.
   scool50..69 (Generic Fanuc 3X Mill.pst l.407-426) - coolant 1 M8 / M9, 2 M7 / M9, 3 (thru-tool) M88 / M89,
   4-10 "M8(CoolantN=ON)" / "M9(CoolantN=OFF)". 2026-11-04 the user's rule (user_decisions.md): as the stock post -
   thru-tool and the custom coolants were refused before (Q13); LinuxCNC itself has no M88 / M89 (its own M-codes stop
   at M7 / M8 / M9) */
static const char *cool_code(int k, int on) {
    static const char *const tab[10][2] = {   /* { off, on } */
        { "M9", "M8" },
        { "M9", "M7" },
        { "M89", "M88" },
        { "M9(Coolant4=OFF)", "M8(Coolant4=ON)" },
        { "M9(Coolant5=OFF)", "M8(Coolant5=ON)" },
        { "M9(Coolant6=OFF)", "M8(Coolant6=ON)" },
        { "M9(Coolant7=OFF)", "M8(Coolant7=ON)" },
        { "M9(Coolant8=OFF)", "M8(Coolant8=ON)" },
        { "M9(Coolant9=OFF)", "M8(Coolant9=ON)" },
        { "M9(Coolant10=OFF)", "M8(Coolant10=ON)" } };
    return k >= 0 && k < 10 ? tab[k][on ? 1 : 0] : NULL;
}

/* ------------------------------------------------------------------------------------------------ drill post text
   (batch 8)
   DrillCyclePageBase (decomp/mcmill_all.c l.444402 EnableControls, InitPage l.444621) labels its controls from the post
   text page 0x27 entry cycle x 12 + k (k 1 cycle name, 3 Dwell, 7 1st peck, 8 Subsequent peck, 9 Peck clearance,
   10 Retract amount, 11 Shift) through CPostText::SetDlgControlTextFromIndex (decomp/mcpost_parse.c l.8356):
   - no text in the post -> the default (mccoreres 7645 + 11 cycle + k - 1, CPostText::ManageTextDrill l.5214) and
     enabled;
   - "" -> the default and DISABLED;
   - any other text -> that text and enabled.
   The post text: the [CTRL_MILL|GENERIC FANUC 3X MILL] pages of Generic Fanuc 3X Mill.pst (shared mcamx9/mill/Posts,
   l.1868-1931; [CTRL_MILL|DEFAULT] l.1792-1855 holds the same), the post the user's parts name - BRACKET4.mcx-9 embeds
   the same [CTRL_MILL|DEFAULT] text (file offset 585130). Ours: the part's own embedded control text is not read (its
   control page is compressed, NOT DECODED); this table stands in for it. */
static const struct { int cyc, k; const char *s; } DRILL_PT[] = {
    { 0, 1, "Drill/Counterbore" }, { 0, 7, "" }, { 0, 8, "" }, { 0, 9, "" }, { 0, 10, "" }, { 0, 11, "" },
    { 1, 3, "" }, { 1, 7, "Peck" }, { 1, 8, "" }, { 1, 9, "" }, { 1, 10, "" }, { 1, 11, "" },
    { 2, 3, "" }, { 2, 7, "Peck" }, { 2, 8, "" }, { 2, 9, "" }, { 2, 10, "" }, { 2, 11, "" },
    { 3, 3, "" }, { 3, 7, "" }, { 3, 8, "" }, { 3, 9, "" }, { 3, 10, "" }, { 3, 11, "" },
    { 4, 1, "Bore #1 (feed-out)" }, { 4, 7, "" }, { 4, 8, "" }, { 4, 9, "" }, { 4, 10, "" }, { 4, 11, "" },
    { 5, 1, "Bore #2 (stop spindle, rapid out)" }, { 5, 3, "" }, { 5, 7, "" }, { 5, 8, "" }, { 5, 9, "" },
    { 5, 10, "" }, { 5, 11, "" },
    { 6, 1, "Fine Bore (shift)" }, { 6, 7, "" }, { 6, 8, "" }, { 6, 9, "" }, { 6, 10, "" },
    { 7, 1, "Rigid Tapping Cycle" }, { 7, 3, "" }, { 7, 7, "" }, { 7, 8, "" }, { 7, 9, "" }, { 7, 10, "" },
    { 7, 11, "" },
};

/* The label of control K of drill cycle CYC; *ON = 0 when the post disables the control. */
const char *nc_drill_text(int cyc, int k, int *on) {
    static const char *const NAME[20] = {   /* mccoreres 7645 + 11 n */
        "Drill/Counterbore", "Peck Drill", "Chip Break", "Tap", "Bore #1", "Bore #2", "Misc. #1", "Misc. #2",
        "Custom cycle 9", "Custom cycle 10", "Custom Cycle 11", "Custom Cycle 12", "Custom Cycle 13",
        "Custom Cycle 14", "Custom Cycle 15", "Custom Cycle 16", "Custom Cycle 17", "Custom Cycle 18",
        "Custom Cycle 19", "Custom Cycle 20" };
    static const char *const ROW[12] = {    /* mccoreres 7646.. 7655 */
        "", "", "Feed rate", "Dwell", "Clearance...", "Retract...", "Depth...", "First peck",
        "Subsequent peck", "Peck clearance", "Retract amount", "Shift" };
    if (on)
        *on = 1;
    if (cyc < 0 || cyc > 19 || k < 1 || k > 11)
        return "";
    const char *def = k == 1 ? NAME[cyc] : ROW[k];
    for (int i = 0; i < (int)(sizeof DRILL_PT / sizeof DRILL_PT[0]); i++)
        if (DRILL_PT[i].cyc == cyc && DRILL_PT[i].k == k) {
            if (!DRILL_PT[i].s[0]) {   /* "": the default, disabled */
                if (on)
                    *on = 0;
                return def;
            }
            return DRILL_PT[i].s;
        }
    return def;
}

/* ------------------------------------------------------------------------------------------------ event list */
typedef struct {
    Ev *e;
    int n, cap;
} EvList;

static void ev_push(EvList *l, Ev e) {
    if (l->n == l->cap) {
        l->cap = l->cap ? 2 * l->cap : 1024;
        l->e = xrealloc(l->e, l->cap * sizeof(Ev));
    }
    l->e[l->n++] = e;
}

/* the (u, v) axes of each arc plane (PLANE_XY, PLANE_YZ, PLANE_XZ); the depth axis is 3 - u - v */
static const int AX[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };

/* 1 when TOOL is in the program's tool list (TNUMS, NTN entries). */
static int tool_listed(const int *tnums, int ntn, int tool) {
    int seen = 0;
    for (int k = 0; k < ntn; k++)
        if (tnums[k] == tool)
            seen = 1;
    return seen;
}

/* little-endian fields of an NCI record: double, i32, i16 */
static double rd_d(const uint8_t *b, int o) {
    double v;
    memcpy(&v, b + o, 8);
    return v;
}
static int rd_i(const uint8_t *b, int o) {
    int32_t v;
    memcpy(&v, b + o, 4);
    return v;
}
static int rd_h(const uint8_t *b, int o) {
    int16_t v;
    memcpy(&v, b + o, 2);
    return v;
}

/* -------------------------------------------------------------------------------------------- events of a stored op */

/* The NCI section of one stored op (id OP_ID) as events, appended to OUT. D / LEN = the NCI bytes, OFF = where to start
   reading. Also collects the tool names (20001) of new tool numbers into TNAMES / TNUMS / *NTN and the material
   (20016) into MATERIAL (MN bytes; may be NULL). */
static void stored_events(const uint8_t *d, size_t len, size_t off, int op_id, EvList *out,
                          char tnames[][TOOL_NAME_LEN], int *tnums, int *ntn, char *material, size_t mn) {
    uint8_t buf[NCI_REC_BUF];
    int in_op = 0;                                           /* inside this op's section */
    char tname[TOOL_NAME_LEN] = "";
    double drill_pz = 0;                                     /* the drill cycle's point z (from its 81 record) */
    double cur[3] = { 0, 0, 0 };
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);  /* record size */
        if (sz == 0 || sz > NCI_REC_MAX || off + 2 + sz > len)
            break;
        memset(buf, 0, sizeof buf);
        memcpy(buf, d + off + 2, sz < sizeof buf - 1 ? sz : sizeof buf - 1);
        off += 2 + sz;
        int g = rd_h(buf, 0);                                /* the record code */
        if (g == NCI_OP_START) {
            in_op = rd_i(buf, 0x1c) == op_id;
            continue;
        }
        if (!in_op)
            continue;
        if (g == NCI_OP_END) {
            in_op = 0;
            continue;
        }
        Ev e;
        memset(&e, 0, sizeof e);
        e.op = op_id;
        if (g == NCI_TOOL_NAME) {                            /* tool name (leading spaces kept, trailing removed) */
            if (buf[0x18] != 0xfe) {
                snprintf(tname, sizeof tname, "%s", (const char *)buf + 0x18);
                size_t L = strlen(tname);
                while (L && tname[L - 1] == ' ')
                    tname[--L] = 0;
            }
        } else if (g == NCI_MATERIAL) {
            if (buf[0x18] && buf[0x18] != 0xfe && material) {
                snprintf(material, mn, "%s", (const char *)buf + 0x18);
            }
        } else if (g >= NCI_TOOL_FIRST && g <= NCI_TOOL_LAST) {   /* tool change */
            e.kind = EV_TOOL;
            e.tool = rd_i(buf, 0x24);
            e.rpm = rd_i(buf, 0x38);
            e.feed = rd_d(buf, 0x40);
            for (int k = 0; k < 3; k++)
                e.p[k] = cur[k] = rd_d(buf, 0x50 + 8 * k);
            if (!tool_listed(tnums, *ntn, e.tool) && *ntn < MAX_TOOLS) {
                tnums[*ntn] = e.tool;
                snprintf(tnames[*ntn], TOOL_NAME_LEN, "%s", tname);
                (*ntn)++;
            }
            ev_push(out, e);
        } else if (g == 0 || g == 1) {                       /* G0 / G1 (feed < 0: rapid) */
            double feed = rd_d(buf, 0x38);
            e.kind = feed < 0 ? EV_RAPID : EV_FEED;
            e.feed = feed;
            {
                int cc = rd_h(buf, 0x18);                    /* NCI cc (formats/mcam.md G0/G1 +0x18) */
                if (cc == CC_LEFT || cc == CC_RIGHT || cc == CC_OFF) {
                    e.cc = cc;
                    e.ccst = 1;
                }
            }
            for (int k = 0; k < 3; k++)
                e.p[k] = cur[k] = rd_d(buf, 0x20 + 8 * k);
            ev_push(out, e);
        } else if (g == 2 || g == 3) {                       /* G2 / G3 arc */
            int pl = rd_h(buf, 0x18);
            if (pl < 0 || pl > 2)
                pl = 0;
            int u = AX[pl][0], v = AX[pl][1], dp = 3 - u - v;
            e.kind = g;
            e.plane = pl;
            e.feed = rd_d(buf, 0x50);
            {
                int cc = rd_h(buf, 0x1a);                    /* G2 / G3 +0x1a */
                if (cc == CC_LEFT || cc == CC_RIGHT || cc == CC_OFF) {
                    e.cc = cc;
                    e.ccst = 1;
                }
            }
            e.p[u] = rd_d(buf, 0x20);
            e.p[v] = rd_d(buf, 0x28);
            e.p[dp] = rd_d(buf, 0x30);
            e.c[u] = rd_d(buf, 0x38);
            e.c[v] = rd_d(buf, 0x40);
            e.c[dp] = cur[dp];
            /* decisions batch dec2 (Q9): a plane-2 (XZ) arc is in view 2 (mccore FUN_18094b400): its depth +0x30 is
               -y (view_to_world, mccore FUN_180944830 l.355 in decomp/mccore_filter.c; checked: 403 / 403
               STEEPSHALLOW arcs meet the previous line's y only so) and its G2 / G3 code is the reverse of the
               direction in the (x, z) frame (FUN_180944830 l.346-352 swaps 2 / 3 for view 2) - kept here as the
               (x, z)-frame direction, as the filter's own arcs (geo) */
            if (pl == PLANE_XZ) {
                e.p[dp] = -e.p[dp];
                e.kind = g == 2 ? EV_CCW : EV_CW;
            }
            for (int k = 0; k < 3; k++)
                cur[k] = e.p[k];
            ev_push(out, e);
        } else if (g == NCI_DRILL_FIRST || g == NCI_DRILL_NEXT) {   /* drill point: 81 first of a cycle, 100 each
                                                                       further one */
            e.kind = EV_DRILL;
            e.p[0] = rd_d(buf, 0x20);
            e.p[1] = rd_d(buf, 0x28);
            e.p[2] = rd_d(buf, 0x30);
            e.feed = rd_d(buf, g == NCI_DRILL_FIRST ? 0x40 : 0x50);
            /* bnci_wr_81 / bnci_wr_100 (formats/mcmill_2d_engines.md 5.3-5.4): +0x68 / +0x38 = initial - point z,
               +0x70 / +0x40 = R plane - point z; point z = initial point z (+0x98) - (+0x68) from the cycle's 81
               record */
            if (g == NCI_DRILL_FIRST)
                drill_pz = rd_d(buf, 0x98) - rd_d(buf, 0x68);
            e.rplane = drill_pz + rd_d(buf, g == NCI_DRILL_FIRST ? 0x70 : 0x40);
            if (g == NCI_DRILL_FIRST) {   /* bnci_wr_81 (mccore): +0x18 cycle (op +0x1228), +0x48 1st peck */
                e.cyc = rd_h(buf, 0x18);
                e.peck = e.cyc == CYC_PECK || e.cyc == CYC_CHIP_BREAK ? rd_d(buf, 0x48) : 0;
                e.dwell = rd_d(buf, 0x38);                   /* batch 8: dwell (formats/mcmill_2d_engines.md 5.3) */
                /* backplotbnci E_nci_mill_81 = this record - 8 (cycle +0x10, hole +0x18, feed +0x38, R +0x60 / +0x68,
                   S +0x80) */
                e.has_s = 1;
                e.s[0] = rd_d(buf, 0x88);
                e.s[1] = rd_d(buf, 0x90);
                e.s[2] = rd_d(buf, 0x98);
                e.rdist = rd_d(buf, 0x68) - rd_d(buf, 0x70);
            }
            cur[0] = e.p[0];
            cur[1] = e.p[1];
            cur[2] = e.rplane;
            ev_push(out, e);
        } else if (g == NCI_DRILL_END) {
            e.kind = EV_DRILL_END;
            ev_push(out, e);
        }
    }
}

/* ------------------------------------------------------------------------------------------------ fitting arcs into
   generated moves (ops made here): longest run of points on one circle (XY), within tol */

/* The circle through the XY points A, B, C: centre (*CX, *CY) and radius *R; 0 when the points are in line. */
static int circ3(const double *a, const double *b, const double *c, double *cx, double *cy, double *r) {
    double d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1]));
    if (fabs(d) < 1e-12)
        return 0;
    double a2 = a[0] * a[0] + a[1] * a[1], b2 = b[0] * b[0] + b[1] * b[1], c2 = c[0] * c[0] + c[1] * c[1];
    *cx = (a2 * (b[1] - c[1]) + b2 * (c[1] - a[1]) + c2 * (a[1] - b[1])) / d;
    *cy = (a2 * (c[0] - b[0]) + b2 * (a[0] - c[0]) + c2 * (b[0] - a[0])) / d;
    *r = hypot(a[0] - *cx, a[1] - *cy);
    return 1;
}

/* The arc starting at move I0 - 1 through moves I0..k: returns the largest k (>= I0 + 2) for which every point lies on
   one circle within TOL, turning one way in steps of at most 45 degrees, with z linear along the sweep and less than a
   full turn; -1 when there is none. *CX, *CY = the centre, *CCW = 1 counter-clockwise. */
static int fit_arc(const Move *m, int i0, int n, double tol, double *cx, double *cy, int *ccw) {
    int best = -1;
    if (m[i0].cc || (i0 + 1 < n && m[i0 + 1].cc))
        return -1;   /* a move carrying a cutter compensation code stays a line of its own */
    if (i0 + 1 < n && (m[i0 + 1].rapid || fabs(m[i0 + 1].feed - m[i0].feed) > 1e-9))
        return -1;   /* the 2nd point too */
    for (int k = i0 + 2; k < n && k < i0 + 720; k++) {
        if (m[k].rapid || fabs(m[k].feed - m[i0].feed) > 1e-9 || m[k].cc)
            break;
        /* the circle through the start, the middle point and point k */
        double a[2] = { m[i0 - 1].p.x, m[i0 - 1].p.y };
        double b[2] = { m[(i0 - 1 + k) / 2].p.x, m[(i0 - 1 + k) / 2].p.y };
        double c[2] = { m[k].p.x, m[k].p.y };
        double x, y, r;
        if (!circ3(a, b, c, &x, &y, &r) || r > 5000 || r < tol * 4)
            break;
        int ok = 1, dir = 0;
        double sweep = 0, sweep_at[730];                     /* sweep so far; at each point (k - i0 < 720) */
        for (int j = i0; j <= k && ok; j++) {
            if (fabs(hypot(m[j].p.x - x, m[j].p.y - y) - r) > tol)
                ok = 0;                                      /* off the circle */
            double ax = m[j - 1].p.x - x, ay = m[j - 1].p.y - y, bx = m[j].p.x - x, by = m[j].p.y - y;
            double cr = ax * by - ay * bx, ang = atan2(cr, ax * bx + ay * by);   /* the turn from point j - 1 to j */
            int s2 = cr > 0 ? 1 : -1;
            if (fabs(ang) > M_PI / 4)
                ok = 0;
            if (!dir)
                dir = s2;
            else if (s2 != dir)
                ok = 0;                                      /* turns the other way */
            sweep += fabs(ang);
            sweep_at[j - i0] = sweep;
            if (r - sqrt(fmax(0, r * r - pow(hypot(bx - ax, by - ay) / 2, 2))) > tol)
                ok = 0;                                      /* the chord leaves the circle by more than tol */
        }
        if (ok) {                                            /* z linear along the sweep */
            double z0 = m[i0 - 1].p.z, dz = m[k].p.z - z0;
            for (int j = i0; j <= k && ok; j++)
                if (fabs(z0 + dz * sweep_at[j - i0] / sweep - m[j].p.z) > tol)
                    ok = 0;
        }
        if (!ok || sweep > 2 * M_PI - 1e-6)
            break;
        best = k;
        *cx = x;
        *cy = y;
        *ccw = dir > 0;
    }
    return best;
}

/* ---------------------------------------------------------------------------------------- events of an op made here */

static void camop_events_at(const McamFile *f, const CamOp *op, int op_id, EvList *out, char tnames[][TOOL_NAME_LEN],
                            int *tnums, int *ntn);

/* The events of an op made / edited here (from its generated moves), appended to OUT; the tool list as stored_events.
   Ops made here keep their moves in world coordinates (the display's); the post writes them in op coordinates, the
   NCI's frame (wpt_to_op, mccore 0x1800a5950: the Tplane's rows . (p - origin); cam2d.c cam_plane_w2v). The Top plane
   at the world origin is the identity: nothing is mapped (every existing program stays as it was).
   to_op = 0: world (Backplot, Verify) */
static void camop_events(const McamFile *f, const CamOp *op, int op_id, EvList *out, char tnames[][TOOL_NAME_LEN],
                         int *tnums, int *ntn, int to_op) {
    if (!to_op || cam_op_top(&op->p)) {
        camop_events_at(f, op, op_id, out, tnames, tnums, ntn);
        return;
    }
    /* a copy of the op with its moves and holes mapped to op coordinates */
    CamOp c = *op;
    c.m = malloc((op->nm ? op->nm : 1) * sizeof(Move));
    memcpy(c.m, op->m, op->nm * sizeof(Move));
    for (int i = 0; i < c.nm; i++) {
        double w[3] = { c.m[i].p.x, c.m[i].p.y, c.m[i].p.z }, v[3];
        cam_plane_w2v(op->p.tplane, op->p.torg, w, v);
        c.m[i].p.x = v[0];
        c.m[i].p.y = v[1];
        c.m[i].p.z = v[2];
    }
    Hole *hv = NULL;
    if (op->nholes) {
        hv = malloc(op->nholes * sizeof(Hole));
        memcpy(hv, op->holes, op->nholes * sizeof(Hole));
        for (int i = 0; i < op->nholes; i++) {
            double w[3] = { hv[i].x, hv[i].y, 0 }, v[3];
            cam_plane_w2v(op->p.tplane, op->p.torg, w, v);
            hv[i].x = v[0];
            hv[i].y = v[1];
        }
        c.holes = hv;
    }
    camop_events_at(f, &c, op_id, out, tnames, tnums, ntn);
    free(c.m);
    free(hv);
}

/* camop_events in the op's own coordinates: a tool change, then drill points or the moves (arcs as one event). */
static void camop_events_at(const McamFile *f, const CamOp *op, int op_id, EvList *out, char tnames[][TOOL_NAME_LEN],
                            int *tnums, int *ntn) {
    const CamParams *p = &op->p;
    if (!op->nm)
        return;

    /* the tool change, at the first move */
    Ev e;
    memset(&e, 0, sizeof e);
    e.op = op_id;
    e.kind = EV_TOOL;
    e.tool = p->tool_num;
    e.rpm = p->rpm;
    e.p[0] = op->m[0].p.x;
    e.p[1] = op->m[0].p.y;
    e.p[2] = op->m[0].p.z;
    ev_push(out, e);
    if (!tool_listed(tnums, *ntn, p->tool_num) && *ntn < MAX_TOOLS) {
        const Tool *t = mcam_tool(f, p->tool_num);
        tnums[*ntn] = p->tool_num;
        snprintf(tnames[*ntn], TOOL_NAME_LEN, "%s", t ? t->name : "");
        (*ntn)++;
    }

    /* a drill op: one drill point per hole, the cycle end, a rapid to the clearance height over the last hole */
    if (p->type == CAM_DRILL) {
        for (int h = 0; h < op->nholes; h++) {
            memset(&e, 0, sizeof e);
            e.op = op_id;
            e.kind = EV_DRILL;
            e.p[0] = op->holes[h].x;
            e.p[1] = op->holes[h].y;
            e.p[2] = cam_drill_z(p);
            e.feed = p->plunge_feed;
            e.rplane = p->r_plane;
            e.peck = p->drill_cycle == CYC_PECK || p->drill_cycle == CYC_CHIP_BREAK ? p->peck : 0;
            e.cyc = p->drill_cycle;
            e.dwell = p->drill_dwell;
            ev_push(out, e);
        }
        memset(&e, 0, sizeof e);
        e.op = op_id;
        e.kind = EV_DRILL_END;
        ev_push(out, e);
        memset(&e, 0, sizeof e);
        e.op = op_id;
        e.kind = EV_RAPID;
        e.feed = -1;
        e.p[0] = op->holes[op->nholes - 1].x;
        e.p[1] = op->holes[op->nholes - 1].y;
        e.p[2] = p->clearance;
        ev_push(out, e);
        return;
    }

    /* the moves */
    const Move *m = op->m;
    int n = op->nm;
    for (int k = 1; k < n; k++) {
        if (p->arcs && !m[k].rapid && m[k].arc > 0) {   /* an engine arc record (cam2d.c mb_tag_arc): its own centre */
            int en = k;                                  /* the arc's last move */
            while (en + 1 < n && m[en + 1].arc == m[k].arc && !m[en + 1].rapid)
                en++;
            memset(&e, 0, sizeof e);
            e.op = op_id;
            e.kind = m[k].cw ? EV_CW : EV_CCW;
            e.plane = m[k].apl;
            e.feed = m[k].feed;
            e.fitted = 0;
            e.geo = 1;
            e.p[0] = m[en].p.x;
            e.p[1] = m[en].p.y;
            e.p[2] = m[en].p.z;
            e.c[0] = m[k].cx;
            e.c[1] = m[k].cy;
            e.c[2] = m[k].apl ? m[k].cz : m[k - 1].p.z;
            for (int j = k; j <= en; j++)
                if (m[j].cc)
                    e.cc = m[j].cc;   /* batch 7: a code on an arc is refused below, never dropped */
            ev_push(out, e);
            k = en;
            continue;
        }
        /* batch 10: a filtered op's lines stay lines (Mastercam's filter output) */
        if (p->arcs && !m[k].rapid && !op->filtered) {
            double cx = 0, cy = 0;
            int ccw = 0;
            int en = fit_arc(m, k, n, fmax(0.003, p->tol * 1.2), &cx, &cy, &ccw);
            if (en >= k + 2) {
                memset(&e, 0, sizeof e);
                e.op = op_id;
                e.kind = ccw ? EV_CCW : EV_CW;
                e.plane = 0;
                e.feed = m[k].feed;
                e.fitted = 1;
                e.p[0] = m[en].p.x;
                e.p[1] = m[en].p.y;
                e.p[2] = m[en].p.z;
                e.c[0] = cx;
                e.c[1] = cy;
                e.c[2] = m[k - 1].p.z;
                ev_push(out, e);
                k = en;
                continue;
            }
        }
        /* a line */
        memset(&e, 0, sizeof e);
        e.op = op_id;
        e.kind = m[k].rapid ? EV_RAPID : EV_FEED;
        e.feed = m[k].rapid ? -1 : m[k].feed;
        e.p[0] = m[k].p.x;
        e.p[1] = m[k].p.y;
        e.p[2] = m[k].p.z;
        e.cc = m[k].cc;
        if (e.cc)
            e.dnum = p->tool_num;   /* decisions batch dec2 (Q30, ours for LinuxCNC): D<tool number> - LinuxCNC's D word
                                       selects a tool and takes its table diameter (interp_convert.cc
                                       convert_cutter_compensation_on); Mastercam's pst writes the diameter offset
                                       number (tloffno$, TL +0x28 = op record 0x370), which equals the tool number in
                                       all 205 user ops, so the output is the same for them (formats/post.md) */
        ev_push(out, e);
    }
}

/* ------------------------------------------------------------------------------------------------ output */

/* The program writer: the output file, the block number and the modal state. */
typedef struct {
    FILE *f;
    int n;                                    /* the next block number */
    char G[8], X[24], Y[24], Z[24], F[24];    /* modal words as last written ("" = write the next one) */
    int S, have_S;                            /* the spindle speed last written */
    int cycle;                                /* a drill cycle is active */
    int tapping;                              /* a tap cycle was written: G94 again at its end */
    int dec, fdec;                            /* decimals of coordinates / of feeds */
} W;

/* V rounded half-up to DEC decimals (no -0). */
static double rnd(double v, int dec) {
    double s = pow(10, dec), r = floor(fabs(v) * s + 0.5 + 1e-9) / s;
    return v < 0 && r != 0 ? -r : r;
}

/* V as a G-code number into O (char[24]): half-up, no trailing zeros: "15.", ".299", "-1.2" */
static void fmtn(char *o, double v, int dec) {
    char t[48];
    snprintf(t, sizeof t, "%.*f", dec, rnd(v, dec));
    if (dec > 0) {                                       /* trailing zeros off */
        char *e = t + strlen(t) - 1;
        while (*e == '0')
            *e-- = 0;
    } else
        strcat(t, ".");
    char *q = t;                                         /* leading zero off: ".5", "-.5" */
    if (!strncmp(q, "0.", 2))
        q++;
    else if (!strncmp(q, "-0.", 3)) {
        q[1] = '-';
        q++;
    }
    if (!strcmp(q, "-.") || !strcmp(q, "."))
        q = "0.";
    snprintf(o, 24, "%s", q);   /* every caller's buffer is char[24]; values are range-checked first */
}

/* Writes one numbered block: N100, N102, ... back to N100 after N9998. */
static void blk(W *w, const char *s) {
    fprintf(w->f, "N%d %s\n", w->n, s);
    w->n = w->n < 9998 ? w->n + 2 : 100;
}

/* batch 11 (MCAM_COOLANT=1): the X-style coolant entries of position POS (0 before, 1 with, 2 after) - pcant_out
   (Generic Fanuc 3X Mill.pst l.1614): an on code only while that coolant is off ("with": appended to APP, else its own
   block), an off code only while some coolant is on (always its own block). *MASK = the coolants on (bit k). */
static void cool_pos(W *w, const OpCool *c, int pos, int *mask, char *app, size_t an) {
    for (int i = 0; i < c->n; i++) {
        if (c->e[i].pos != pos)
            continue;
        int k = c->e[i].k, bit = 1 << k;
        const char *cd = cool_code(k, c->e[i].on);
        if (!cd)
            continue;
        if (c->e[i].on) {
            if (*mask & bit)
                continue;
            *mask |= bit;
            if (app && pos == COOL_WITH)
                snprintf(app + strlen(app), an - strlen(app), " %s", cd);
            else
                blk(w, cd);
        } else {
            if (!*mask)
                continue;
            *mask &= ~bit;
            blk(w, cd);
        }
    }
}

/* pretract l.770: each coolant still on, its off code */
static void cool_off_all(W *w, int *mask) {
    for (int k = 0; k < 10; k++)
        if (*mask & (1 << k)) {
            const char *cd = cool_code(k, 0);
            if (cd)
                blk(w, cd);
        }
    *mask = 0;
}

/* Forgets the modal words (after a tool change every word is written again). */
static void mod_clear(W *w) {
    w->G[0] = w->X[0] = w->Y[0] = w->Z[0] = w->F[0] = 0;
    w->have_S = 0;
    w->cycle = 0;
}

static const char *words_pre;   /* G40 / G41 D.. / G42 D.. after the motion word (Generic Fanuc 3X Mill.pst: sgcode,
                                   sgabsinc, pccdia) */
static const char *words_plane; /* batch 10: G18 / G19 / G17 before the motion word (the pst's sgplane, fstrsel sg17
                                   plane$) */

/* The words of a motion block into LINE: modal words: G (unless forced), changed X/Y/Z, extra, changed F (not on
   rapids). */
static void words(W *w, int g, const double *xyz, const char *extra, const double *feed, char *line) {
    char t[24], gs[8];
    line[0] = 0;
    snprintf(gs, sizeof gs, "%d", g);
    if (words_plane) {
        sprintf(line + strlen(line), "%s ", words_plane);
        w->G[0] = 0;
    }
    if (strcmp(w->G, gs) || words_pre) {
        sprintf(line + strlen(line), "G%d ", g);
        strcpy(w->G, gs);
    }
    if (words_pre)
        sprintf(line + strlen(line), "%s ", words_pre);
    char *mods[3] = { w->X, w->Y, w->Z };
    for (int k = 0; k < 3; k++) {
        fmtn(t, xyz[k], w->dec);
        if (strcmp(mods[k], t)) {
            sprintf(line + strlen(line), "%c%s ", "XYZ"[k], t);
            strcpy(mods[k], t);
        }
    }
    if (extra && extra[0])
        sprintf(line + strlen(line), "%s ", extra);
    if (feed && g != 0) {
        fmtn(t, *feed, w->fdec);
        if (strcmp(w->F, t)) {
            sprintf(line + strlen(line), "F%s ", t);
            strcpy(w->F, t);
        }
    }
    size_t L = strlen(line);
    if (L && line[L - 1] == ' ')
        line[L - 1] = 0;
}

/* ------------------------------------------------------------------------------------------------ planes / work offset
   The op's planes on this 3-axis post: the Tplane must be the WCS (else rotary / multi-axis output, not supported); the
   work offset: Manual n -> the Generic Fanuc 3X Mill.pst pwcs rule (workofs$ < 6: G(54 + n), else G54.1 P(n - 5));
   Automatic: G54 when the WCS is Top (every program so far), else NOT DECODED (BuildWorkOffsetMap) - refused */

/* 1 when the op's tool plane is its WCS: the same plane: matrix (system id) and origin */
static int op_tplane_wcs(const CamParams *p) {
    int t = p->tplane <= 0 ? 1 : p->tplane, w = p->wcs <= 0 ? 1 : p->wcs;
    return t == w && !memcmp(p->torg, p->worg, sizeof p->torg);
}

/* 1 when this post can write the op's planes / work offset; else 0 + ERR (I = the op's index). */
static int op_postable(const CamOp *op, char *err, size_t errn, int i) {
    const CamParams *p = &op->p;
    if (!op_tplane_wcs(p)) {
        snprintf(err, errn, "operation %d: its tool plane (%d) is not its WCS (%d) - this 3-axis post has no rotary / "
                 "multi-axis output", i + 1, p->tplane <= 0 ? 1 : p->tplane, p->wcs <= 0 ? 1 : p->wcs);
        return 0;
    }
    if (!p->wofs_manual && p->wcs > 1) {
        snprintf(err, errn, "operation %d: an Automatic work offset for a WCS other than Top is not decoded - set a "
                 "Manual work offset on its Planes page", i + 1);
        return 0;
    }
    return 1;
}

/* The work offset word of the op (P may be NULL: G54). */
static void wcs_word(const CamParams *p, char *o, size_t n) {
    if (!p || !p->wofs_manual || p->wofs <= 0)
        snprintf(o, n, "G54");
    else if (p->wofs < 6)
        snprintf(o, n, "G%d", 54 + p->wofs);
    else
        snprintf(o, n, "G54.1 P%d", p->wofs - 5);
}

/* ------------------------------------------------------------------------------------------------ the post */

/* 1 when the part can be posted at all: its units are known and its file has not changed on disk; else 0 + ERR. */
static int post_check_part(const McamFile *f, char *err, size_t errn) {
    if (f->metric == 2) {
        snprintf(err, errn, "the part mixes inch and metric operations");
        return 0;
    }
    if (f->metric < 0 && f->path[0]) {   /* units come from its stored toolpath; G20 / G21 must not be guessed */
        snprintf(err, errn, "the part's units are not known (it has no Mastercam toolpath to read them from)");
        return 0;
    }
    if (f->path[0] && f->nci_off) {      /* the stored toolpaths are read again at the offsets found on load */
        struct stat st;
        if (stat(f->path, &st) != 0 || (long long)st.st_size != f->file_size
            || (long long)st.st_mtime != f->file_mtime) {
            snprintf(err, errn, "the part file changed on disk since it was opened - open it again before posting");
            return 0;
        }
    }
    return 1;
}

/* The stored NCI for the post: the whole part file (*LEN bytes); NULL when the part has none or it cannot be opened.
   readability: kept apart from read_part_nci (Backplot / extents) on purpose, as it was: here an empty file gives a
   1-byte buffer and a short read keeps the buffer with *LEN = 0 (the ops are then reported as having no toolpath,
   not the file as unreadable). */
static uint8_t *post_read_part(const McamFile *f, size_t *len) {
    uint8_t *d = NULL;
    size_t n = 0;
    if (f->path[0] && f->nci_off) {
        FILE *in = fopen(f->path, "rb");
        if (in) {
            fseek(in, 0, SEEK_END);
            n = (size_t)ftell(in);
            fseek(in, 0, SEEK_SET);
            d = xmalloc(n ? n : 1);
            if (fread(d, 1, n, in) != n)
                n = 0;
            fclose(in);
        }
    }
    *len = n;
    return d;
}

/* The events of the ops to post (O->only picks them) into EV, in Operations Manager order (F.ops order): ours from
   their moves, Mastercam's from the stored NCI (D / LEN = the part file from post_read_part). Fills the tool list and
   the material as stored_events. Returns the count of ops posted; 0 + ERR (EV freed) when an op is refused or has no
   toolpath. */
static int post_collect_events(const McamFile *f, CamOp **camops, int ncamops, const NcPostOpts *o, int inch,
                               const uint8_t *d, size_t len, EvList *ev, char tnames[][TOOL_NAME_LEN], int *tnums,
                               int *ntn, char *material, size_t mn, char *err, size_t errn) {
    int nposted = 0, nmissing = 0, d_failed = f->path[0] && f->nci_off && !d;
    char missing[200] = "";                              /* the ops without a toolpath: "3, 5" */
    for (int i = 0; i < f->nops; i++) {
        if (o->only && !o->only[i])
            continue;
        const CamOp *mine = NULL;
        for (int j = 0; j < ncamops; j++)
            if (camops[j]->op_index == i)
                mine = camops[j];
        /* a lathe op (stored or regenerated here, CAM_LFACE): this is a 3-axis mill post - lathe posting is not decoded
           (decision P4: lathe posts skipped); its X / Z moves would come out as mill G-code. Refused, never posted. The
           lathe opcodes: mccore op_is_lathe @1800c6060 (nci_write.c) */
        if ((mine && mine->p.type == CAM_LFACE)
            || (!mine && ((uint16_t)(f->ops[i].opcode - 0x3c) <= 12 || (uint16_t)(f->ops[i].opcode - 0xc9) <= 0x23))) {
            free(ev->e);
            snprintf(err, errn, "operation %d is a lathe operation (%s): lathe posts are not supported here (decision "
                     "P4) - leave it out of the post",
                     i + 1, mine ? cam_type_label(mine->p.type) : f->ops[i].type[0] ? f->ops[i].type : "lathe");
            return 0;
        }
        int before = ev->n;
        if (!mine && f->ops[i].stale) {                    /* its tool was changed after Mastercam computed it */
            free(ev->e);
            snprintf(err, errn, "operation %d's toolpath was made for a tool that has since been changed - regenerate "
                     "it first", i + 1);
            return 0;
        }
        if (mine && !op_postable(mine, err, errn, i)) {
            free(ev->e);
            return 0;
        }
        if (mine && mine->gen_units && mine->gen_units - 1 != !inch) {
            /* computed before a units change (CamOp.gen_units). Mastercam keeps such an op as it is - not dirty,
               parameters not converted (formats/units.md 9); its NCI keeps the units it was made in (1016 +0x44) */
            free(ev->e);
            snprintf(err, errn, "operation %d's toolpath was computed in %s and the part is now in %s - regenerate it "
                     "first%s", i + 1,
                     mine->gen_units == 2 ? "mm" : "inch", inch ? "inch" : "mm",
                     mine->dirty ? "" : " (not dirty: Regenerate all selected)");
            return 0;
        }
        if (!mine && (f->ops[i].nci || d) && (f->nci_metric == 0 || f->nci_metric == 1) && f->nci_metric != !inch) {
            /* a stored toolpath keeps the units Mastercam made it in (1016 met_tool); after a units change
               (cui_units_set) it is not converted (no conversion of operations is decoded, formats/units.md) - refused,
               never posted at the wrong scale */
            free(ev->e);
            snprintf(err, errn, "operation %d's toolpath is in %s and the part is now in %s - regenerate it first",
                     i + 1, f->nci_metric ? "mm" : "inch", inch ? "inch" : "mm");
            return 0;
        }
        if (mine)
            camop_events(f, mine, f->ops[i].id, ev, tnames, tnums, ntn, 1);
        else if (f->ops[i].nci)                          /* rewritten here (Reverse toolpath, nci_reverse.c) */
            stored_events(f->ops[i].nci, f->ops[i].nci_n, 0, f->ops[i].id, ev, tnames, tnums, ntn, material, mn);
        else if (d)
            stored_events(d, len, f->nci_off, f->ops[i].id, ev, tnames, tnums, ntn, material, mn);
        if (ev->n > before)
            nposted++;
        else {
            size_t l = strlen(missing);
            if (l < sizeof missing - 8)
                snprintf(missing + l, sizeof missing - l, "%s%d", nmissing ? ", " : "", i + 1);
            nmissing++;
        }
    }
    if (!nposted) {
        free(ev->e);
        snprintf(err, errn, "no toolpaths to post");
        return 0;
    }
    if (nmissing) {                                      /* never a program with operations silently left out */
        free(ev->e);
        snprintf(err, errn, "operation(s) %s have no toolpath to post%s - regenerate them or leave them out",
                 missing, d_failed ? " (the part file could not be read again)" : "");
        return 0;
    }
    return nposted;
}

/* ---- the checks of the event list: each returns 1, or 0 + ERR for the first event it refuses */

/* Nothing absurd reaches the program: finite coordinates / feeds within COORD_LIMIT, a spindle speed, a feed on every
   cutting move; XZ / YZ arcs only with MCAM_ARC_FILTER_PLANES=1. */
static int check_values(const EvList *ev, char *err, size_t errn) {
    for (int i = 0; i < ev->n; i++) {
        const Ev *e = &ev->e[i];
        const char *bad = NULL;
        for (int k = 0; k < 3 && !bad; k++)
            if (!isfinite(e->p[k]) || fabs(e->p[k]) > COORD_LIMIT || !isfinite(e->c[k]) || fabs(e->c[k]) > COORD_LIMIT)
                bad = "an invalid coordinate";
        if (!bad && (!isfinite(e->feed) || fabs(e->feed) > COORD_LIMIT || !isfinite(e->rplane)
                     || fabs(e->rplane) > COORD_LIMIT || !isfinite(e->peck)))
            bad = "an invalid feed / R plane";
        if (!bad && e->kind == EV_TOOL && e->rpm == 0)
            bad = "spindle speed 0";
        if (!bad && (e->kind == EV_FEED || e->kind == EV_CW || e->kind == EV_CCW || e->kind == EV_DRILL)
            && !(e->feed > 0))
            bad = "a cutting move without a feed rate";
        if (bad) {
            snprintf(err, errn, "operation %d has %s", e->op, bad);
            return 0;
        }
        /* decisions batch dec2 (Q9 / Q7): XZ / YZ arcs are posted (G18 / G19) only with MCAM_ARC_FILTER_PLANES=1; a
           stored toolpath with them (corpus 3D ops: NCI arc plane 1 / 2) is refused without it (before: written as G17
           arcs - wrong) */
        if ((e->kind == EV_CW || e->kind == EV_CCW) && e->plane && !cam_arc_filter_planes_on()) {
            snprintf(err, errn, "operation %d: the toolpath has %s arcs (G%d) - posted only with "
                     "MCAM_ARC_FILTER_PLANES=1 (formats/post.md \"Arc filter\")",
                     e->op, e->plane == 2 ? "XZ" : "YZ", e->plane == 2 ? 18 : 19);
            return 0;
        }
    }
    return 1;
}

/* Cutter compensation in control (batch 5, formats/post.md): only with MCAM_CUTTER_COMP=1; never from a stored NCI
   (its cc codes and 1004 records are not ported: before batch 5 such an op was posted without G41 / G42, on the chain -
   ours: refused); LinuxCNC's rules (src/emc/rs274ngc/interp_convert.cc 2.9): the move with G41 / G42 longer than the
   tool radius in XY (convert_straight_comp1 "Length of cutter compensation entry move is not greater than the tool
   radius"), the move after G40 straight ("must be straight, not an arc": the G40 move here is always a line) */
static int check_cutter_comp(const McamFile *f, CamOp **camops, int ncamops, const EvList *ev, char *err,
                             size_t errn) {
    int on = nc_cutter_comp_on();
    double prv[3] = { 0, 0, 0 };                         /* the previous move's end */
    for (int i = 0; i < ev->n; i++) {
        const Ev *e = &ev->e[i];
        if (e->cc && e->ccst) {
            snprintf(err, errn, "operation %d: Mastercam's toolpath uses cutter compensation in control (G41 / "
                     "G42) - posting it from the stored toolpath is not supported here; regenerate it in this "
                     "program", e->op);
            return 0;
        }
        if (e->cc && !on) {
            snprintf(err, errn, "operation %d uses cutter compensation in control / wear (G41 / G42): set "
                     "MCAM_CUTTER_COMP=1 to post it (formats/post.md), or set its compensation to Computer", e->op);
            return 0;
        }
        if (e->cc == CC_LEFT || e->cc == CC_RIGHT) {   /* decisions batch dec2 (Q29): Control only on LinuxCNC - Wear /
                                                          Reverse wear refused */
            const CamOp *co = camop_by_id(f, camops, ncamops, e->op);
            int ct = co ? op_cc_type(&co->p) : 0;
            if (ct == COMP_WEAR || ct == COMP_REVERSE_WEAR) {
                snprintf(err, errn, "operation %d uses %s compensation: its path is already offset and LinuxCNC "
                         "would add the tool table radius again - this post supports Control only "
                         "(formats/post.md)", e->op, ct == 2 ? "Wear" : "Reverse wear");
                return 0;
            }
        }
        if (e->cc && (e->kind == EV_CW || e->kind == EV_CCW)) {
            /* batch 7 (ours): Slot Mill's record 2 / last record can be its entry / exit arc (FUN_180218410);
               LinuxCNC turns compensation on / off on straight moves only */
            snprintf(err, errn, "operation %d: cutter compensation (G%d) would start or end on an arc - LinuxCNC "
                     "refuses it; give the pass an entry / exit line (Slot Mill: Perpendicular entry)",
                     e->op, e->cc == CC_OFF ? 40 : e->cc);
            return 0;
        }
        if (e->cc == CC_LEFT || e->cc == CC_RIGHT) {
            const CamOp *co = camop_by_id(f, camops, ncamops, e->op);
            double R = co ? co->p.tool_dia / 2 : 0;      /* the tool radius */
            if (hypot(e->p[0] - prv[0], e->p[1] - prv[1]) <= R) {
                snprintf(err, errn, "operation %d: the move that turns cutter compensation on (G%d) is not longer "
                         "than the tool radius - LinuxCNC refuses it; lengthen the entry line (Lead In/Out)",
                         e->op, e->cc);
                return 0;
            }
        }
        if (e->kind == EV_TOOL || e->kind == EV_RAPID || e->kind == EV_FEED || e->kind == EV_CW
            || e->kind == EV_CCW)
            memcpy(prv, e->p, sizeof prv);
    }
    return 1;
}

/* Drill cycles: by default only G81 / G83 / G73; with DCYC (MCAM_DRILL_CYCLES=1) Tap and Bore #1 too. */
static int check_drill_cycles(const EvList *ev, int dcyc, char *err, size_t errn) {
    for (int i = 0; i < ev->n; i++)
        if (ev->e[i].kind == EV_DRILL && (ev->e[i].cyc > (dcyc ? 4 : 2) || ev->e[i].cyc < 0)) {
            static const char *cn[] = { "", "", "", "Tap", "Bore #1", "Bore #2", "Misc. #1", "Misc. #2" };
            int c = ev->e[i].cyc;
            if (dcyc && c == CYC_BORE2)   /* pbore2$ writes G86 without P; LinuxCNC 2.9 interp_cycles.cc:
                                             NCE_DWELL_TIME_P_WORD_MISSING_WITH_G86 */
                snprintf(err, errn, "operation %d uses the Bore #2 cycle: the post writes G86 without a P word, which "
                         "LinuxCNC refuses", ev->e[i].op);
            else if (dcyc && c == CYC_MISC1)   /* pmisc1$ = sgm1 "G76" (fine bore); LinuxCNC's G76 is the lathe
                                                  threading cycle */
                snprintf(err, errn, "operation %d uses the Fine Bore (shift) cycle: the post writes G76, which is a "
                         "threading cycle in LinuxCNC", ev->e[i].op);
            else if (dcyc && c == CYC_MISC2)   /* pmisc2$: M29 S.. rigid tapping (no M29 in LinuxCNC) */
                snprintf(err, errn, "operation %d uses the Rigid Tapping cycle: the post writes M29, which LinuxCNC "
                         "does not have", ev->e[i].op);
            else
                snprintf(err, errn, "operation %d uses the %s drill cycle, which this post does not output%s",
                         ev->e[i].op, c >= 0 && c < 8 ? cn[c] : "custom",
                         !dcyc && c >= 3 && c <= 4 ? " (MCAM_DRILL_CYCLES=1 posts it, formats/post.md)" : "");
            return 0;
        }
    return 1;
}

/* decisions batch dec1 (formats/user_decisions.md B8-2): a drill op with a dwell (NCI 81 +0x38 / record 0x1250) is
   refused without MCAM_DRILL_CYCLES=1 - before, the default post wrote G81 (or G83 / G73) and dropped the dwell
   silently */
static int check_drill_dwell(const EvList *ev, int dcyc, char *err, size_t errn) {
    if (!dcyc)
        for (int i = 0; i < ev->n; i++)
            if (ev->e[i].kind == EV_DRILL && ev->e[i].dwell != 0) {
                snprintf(err, errn, "operation %d has a dwell of %g s: the default post would write the cycle "
                         "without it; set MCAM_DRILL_CYCLES=1 to post it (G82 / G89 P, formats/post.md \"Drill "
                         "cycles\") or set the dwell to 0", ev->e[i].op, ev->e[i].dwell);
                return 0;
            }
    return 1;
}

/* batch 11: with NCOOL (MCAM_COOLANT=1) every op's coolant must have a LinuxCNC code (op_coolant above) */
static int check_coolant(const McamFile *f, CamOp **camops, int ncamops, const EvList *ev, int ncool, char *err,
                         size_t errn) {
    if (ncool)
        for (int i = 0; i < ev->n; i++)
            if (ev->e[i].kind == EV_TOOL && (i == 0 || ev->e[i].op != ev->e[i - 1].op)) {
                OpCool oc;
                op_coolant(f, camops, ncamops, ev->e[i].op, &oc);
                int bad = 0;                             /* V9 thru: sm08_2 = "M8" (l.399) */
                for (int q = 0; q < oc.n; q++)
                    if (!cool_code(oc.e[q].k, oc.e[q].on))
                        bad = 1;
                if (bad) {
                    snprintf(err, errn, "operation %d turns on %s coolant: LinuxCNC has no code for it (M7 mist / M8 "
                             "flood / M9 off only) - set it to Ignore / Off on the Coolant page",
                             ev->e[i].op, "an unknown");
                    return 0;
                }
            }
    return 1;
}

/* With DCYC (MCAM_DRILL_CYCLES=1) a Tap cycle needs the tool's thread pitch. */
static int check_tap_pitch(const McamFile *f, const EvList *ev, int dcyc, char *err, size_t errn) {
    if (dcyc)
        for (int i = 0; i < ev->n; i++)
            if (ev->e[i].kind == EV_DRILL && ev->e[i].cyc == CYC_TAP) {
                const Tool *tl = NULL;
                int tn = -1;                             /* the tool of the last tool change before it */
                for (int j = i; j >= 0 && tn < 0; j--)
                    if (ev->e[j].kind == EV_TOOL)
                        tn = ev->e[j].tool;
                tl = mcam_tool(f, tn);
                if (!tl || !(tl->pitch > 0)) {
                    snprintf(err, errn, "operation %d: a Tap cycle needs the tool's thread pitch (tool %d has none)",
                             ev->e[i].op, tn);
                    return 0;
                }
            }
    return 1;
}

/* The program header: name, date, part file, material, tool list; then the units and the safety block. */
static void post_header(W *w, FILE *out, const McamFile *f, const NcPostOpts *o, int inch, char *material,
                        char tnames[][TOOL_NAME_LEN], const int *tnums, int ntn) {
    char name[256];
    const char *b = path_sep_r(f->path) ? path_sep_r(f->path) + 1 : f->path;   /* the part file's name */
    snprintf(name, sizeof name, "%s", o->progname ? o->progname : (b[0] ? b : "PROGRAM"));
    {
        char *dot = strrchr(name, '.');
        if (dot && !o->progname)
            *dot = 0;
    }
    upcase(name);
    time_t now = time(NULL);
    struct tm tm = *localtime(&now);
    char ds[64];
    strftime(ds, sizeof ds, "(DATE=DD-MM-YY - %d-%m-%y TIME=HH:MM - %H:%M)", &tm);
    fprintf(out, "(%s)\n%s\n", name, ds);
    {
        extern char cad_doc_name[] __attribute__((weak));
        char up[1100];
        snprintf(up, sizeof up, "%s", f->path[0] || !cad_doc_name ? f->path : cad_doc_name);
        upcase(up);
        fprintf(out, "(MCX FILE - %s)\n", up);
    }
    if (material[0]) {
        upcase(material);
        fprintf(out, "(MATERIAL - %s)\n", material);
    }
    for (int k = 0; k < ntn; k++) {
        char up[TOOL_NAME_LEN];
        snprintf(up, sizeof up, "%s", tnames[k]);
        upcase(up);
        fprintf(out, "( T%d | %s | H%d )\n", tnums[k], up, tnums[k]);
    }
    blk(w, inch ? "G20" : "G21");
    blk(w, "G0 G17 G40 G49 G80 G90");
}

/* ---- the program body */

/* The state of the program body while the events are written. */
typedef struct {
    W w;
    FILE *out;
    const McamFile *f;
    CamOp **camops;
    int ncamops;
    int quad, ij;            /* style ij-quad: arcs broken at the quadrants, I / J words */
    int inch;
    double tiny;             /* arcs with a shorter chord post as G1 */
    int dcyc;                /* batch 8: MCAM_DRILL_CYCLES=1 */
    int ncool;               /* batch 11: MCAM_COOLANT=1 */
    int fortc;               /* batch 12: MCAM_FORCE_TOOL_CHANGE=1 (op_force_tc) */
    int started;             /* the first tool change is written */
    int tool;                /* the tool in the spindle (-1 none) */
    int last_op;             /* the op of the last tool change event */
    double prev[3];          /* where the tool is */
    int cmask, v9on;         /* batch 11 (MCAM_COOLANT=1): X-style coolants on (bit k), V9 coolant on (1 flood,
                                2 mist) */
    int wpl;                 /* batch 10: the plane of the last G18 / G19 arc (0 = G17) */
    char plw[8];             /* its G word */
} Post;

/* The first tool change event of an op: its comment, its coolant and, unless the same tool goes on, the tool change
   (the end of the previous tool first). */
static void post_op_start(Post *P, const Ev *e) {
    W *w = &P->w;
    const McamFile *f = P->f;
    char line[256], t1[24], t2[24], t3[24];
    const char *comment = "";
    const Op *so = stored_op_by_id(f, e->op);
    if (so)
        comment = so->comment;
    const CamOp *co = camop_by_id(f, P->camops, P->ncamops, e->op);
    if (co)
        comment = co->name;
    char cup[128];
    snprintf(cup, sizeof cup, "%s", comment);
    upcase(cup);
    P->last_op = e->op;
    OpCool oc;
    memset(&oc, 0, sizeof oc);
    if (P->ncool)
        op_coolant(f, P->camops, P->ncamops, e->op, &oc);
    int ftc = P->fortc && P->started && e->tool == P->tool
              && op_force_tc(f, P->camops, P->ncamops, e->op);   /* batch 12: 1002 for the same tool */
    if (P->started && (e->tool != P->tool || ftc)) {    /* end of the previous tool */
        cool_off_all(w, &P->cmask);   /* batch 11: pretract - the off code of each coolant on, before "M5" */
        blk(w, "M5");
        blk(w, P->v9on ? "G91 G28 Z0. M9" : "G91 G28 Z0.");
        P->v9on = 0;
        blk(w, "M01");
        mod_clear(w);
    }
    if (P->started && e->tool == P->tool && !ftc) {
        /* the same tool goes on */
        if (cup[0])
            fprintf(P->out, "( %s )\n", cup);
        cool_pos(w, &oc, COOL_BEFORE, &P->cmask, NULL, 0);   /* batch 11: ptlchg0$ - pcan */
        if (P->quad || !w->have_S || e->rpm != w->S) {
            /* < 0 = CCW: sm04 "M4" (fstrsel sm04 spdir2) */
            snprintf(line, sizeof line, "S%d %s", abs(e->rpm), e->rpm < 0 ? "M4" : "M3");
            blk(w, line);
        }
        w->S = e->rpm;
        w->have_S = 1;
        /* to the op's start (its clearance height) first, as Mastercam's post does ("X.. Y.. Z25." then
           "Z10."): without it the next op's first rapid ran straight down to the feed plane on a diagonal */
        {
            char cw[64] = "";
            cool_pos(w, &oc, COOL_WITH, &P->cmask, cw, sizeof cw);   /* "pbld, n$, pscool, e$" (with) */
            if (oc.v9 && oc.v9 != P->v9on) {
                snprintf(cw + strlen(cw), sizeof cw - strlen(cw), " %s", oc.v9 == 2 ? "M7" : "M8");
                P->v9on = oc.v9;
            } else if (P->ncool && !oc.v9 && P->v9on && pv_cool_frompost(pv_machine(f))) {
                snprintf(cw + strlen(cw), sizeof cw - strlen(cw), " M9");   /* scoolant = sm09 */
                P->v9on = 0;
            }
            if (cw[0])
                blk(w, cw + 1);
        }
        words(w, EV_RAPID, e->p, NULL, NULL, line);
        if (line[0])
            blk(w, line);
        cool_pos(w, &oc, COOL_AFTER, &P->cmask, NULL, 0);    /* pcom_movea -> pcan2 */
    } else {
        /* a tool change */
        if (cup[0])
            fprintf(P->out, "( %s )\n", cup);
        cool_pos(w, &oc, COOL_BEFORE, &P->cmask, NULL, 0);   /* batch 11: ptlchg$ - pcan ahead of "T M6" */
        snprintf(line, sizeof line, "T%d M6", e->tool);
        blk(w, line);
        fmtn(t1, e->p[0], w->dec);
        fmtn(t2, e->p[1], w->dec);
        fmtn(t3, e->p[2], w->dec);
        {
            /* the work offset of the op starting here (ours: Top / Automatic = G54) */
            const CamParams *cp = co ? &co->p : NULL;
            char wc[24];
            wcs_word(cp, wc, sizeof wc);
            snprintf(line, sizeof line, "G0 G90 %s X%s Y%s ", wc, t1, t2);
            blk(w, line);
        }
        fprintf(P->out, "%s S%d%s\n", e->rpm < 0 ? "M4" : "M3", abs(e->rpm), P->tool < 0 ? " " : "");
        w->S = e->rpm;
        w->have_S = 1;
        /* tool length: H0 as the user's post (LinuxCNC touch-off); MCAM_H_OFFSET=tool gives H<tool> */
        int h = 0;
        if (env_is("MCAM_H_OFFSET", "tool")) {
            const Tool *tl = mcam_tool(f, e->tool);
            h = tl && tl->len_off > 0 ? tl->len_off : e->tool;
        }
        snprintf(line, sizeof line, "G43 H%d Z%s", h, t3);
        {
            char cw[64] = "";
            cool_pos(w, &oc, COOL_WITH, &P->cmask, cw, sizeof cw);   /* batch 11: pscool on the G43 line (with) */
            if (oc.v9) {
                snprintf(cw + strlen(cw), sizeof cw - strlen(cw), " %s", oc.v9 == 2 ? "M7" : "M8");
                P->v9on = oc.v9;
            }
            snprintf(line + strlen(line), sizeof line - strlen(line), "%s", cw);
        }
        blk(w, line);
        cool_pos(w, &oc, COOL_AFTER, &P->cmask, NULL, 0);    /* pcom_movea -> pcan2 */
        strcpy(w->G, "0");
        strcpy(w->X, t1);
        strcpy(w->Y, t2);
        strcpy(w->Z, t3);
        P->tool = e->tool;
        P->started = 1;
    }
    memcpy(P->prev, e->p, sizeof P->prev);
}

/* A drill point: the cycle block at the first point of a cycle, X / Y moves at the others. */
static void post_drill_point(Post *P, const Ev *e) {
    W *w = &P->w;
    char line[256];
    char xs[24], ys[24], zs[24], rs[24], fs[24], qs[24], wds[80] = "";
    fmtn(xs, e->p[0], w->dec);
    fmtn(ys, e->p[1], w->dec);
    fmtn(zs, e->p[2], w->dec);
    fmtn(rs, e->rplane, w->dec);
    fmtn(fs, e->feed, w->fdec);
    if (strcmp(w->X, xs)) {
        strcat(wds, " X");
        strcat(wds, xs);
    }
    if (strcmp(w->Y, ys)) {
        strcat(wds, " Y");
        strcat(wds, ys);
    }
    if (!w->cycle) {
        char ps[24];
        if (e->peck > 0) {                        /* pchpbrk$ = ppeck$ with sg73 (Generic Fanuc 3X Mill.pst) */
            fmtn(qs, e->peck, w->dec);
            snprintf(line, sizeof line, "G99 %s%s Z%s R%s Q%s F%s", e->cyc == 2 ? "G73" : "G83", wds, zs, rs, qs, fs);
        }
        /* batch 8, MCAM_DRILL_CYCLES=1 only (Generic Fanuc 3X Mill.pst pdrlcommonb l.1071: drlgsel = fsg2(dwell$) +
           drillcyc$ x 2 -> sg81 / sg81d G82 / sg85 G85 / sg85d G89, Tap fsg1(-ss$): sg84 G84 / sg84d G74; pdrill$ /
           pbore1$: ... R, dwell$ ("P" fs2 11: 3 decimals), F; ptap$: a G94 block, then ... R, F = pitch x speed (fs2
           17: inch 2 / metric 3 decimals)) */
        else if (P->dcyc && e->cyc == CYC_DRILL && e->dwell != 0) {
            fmtn(ps, e->dwell, 3);
            snprintf(line, sizeof line, "G99 G82%s Z%s R%s P%s F%s", wds, zs, rs, ps, fs);
        } else if (P->dcyc && e->cyc == CYC_BORE1) {
            if (e->dwell != 0) {
                fmtn(ps, e->dwell, 3);
                snprintf(line, sizeof line, "G99 G89%s Z%s R%s P%s F%s", wds, zs, rs, ps, fs);
            } else
                snprintf(line, sizeof line, "G99 G85%s Z%s R%s F%s", wds, zs, rs, fs);
        } else if (P->dcyc && e->cyc == CYC_TAP) {
            const Tool *tl = mcam_tool(P->f, P->tool);   /* checked above */
            double pit = tl ? tl->pitch : 0;              /* ptap$: met_tool$ / toolismetric conversions = the pitch in
                                                             the program's units */
            if (tl && tl->is_metric && P->inch)
                pit /= MM_PER_INCH;
            else if (tl && !tl->is_metric && !P->inch)
                pit *= MM_PER_INCH;
            char tf[24];
            fmtn(tf, pit * abs(w->S), P->inch ? 2 : 3);
            blk(w, "G94");
            snprintf(line, sizeof line, "G99 %s%s Z%s R%s F%s", w->S < 0 ? "G74" : "G84", wds, zs, rs, tf);
            strcpy(fs, tf);
            w->tapping = 1;
        }
        else
            snprintf(line, sizeof line, "G99 G81%s Z%s R%s F%s", wds, zs, rs, fs);
        blk(w, line);
        w->cycle = 81;
    } else if (wds[0])
        blk(w, wds + 1);
    strcpy(w->X, xs);
    strcpy(w->Y, ys);
    strcpy(w->F, fs);
    strcpy(w->Z, rs);                             /* G99: the tool waits at the R plane between holes */
    P->prev[0] = e->p[0];
    P->prev[1] = e->p[1];
    P->prev[2] = e->rplane;
}

/* The end of a drill cycle: G80 (and G94 after a tap). */
static void post_drill_end(Post *P) {
    W *w = &P->w;
    if (w->cycle) {
        blk(w, "G80");
        w->cycle = 0;
        w->G[0] = 0;
    }
    if (w->tapping) {   /* pcanceldc$: G94 after a tap (tap_feedtype 1); the feed format reset (newfs(15, feed)): the
                           next F is written again */
        blk(w, "G94");
        w->tapping = 0;
        w->F[0] = 0;
    }
}

/* A rapid / feed line, with its cutter compensation word. */
static void post_line(Post *P, const Ev *e) {
    W *w = &P->w;
    char line[256];
    char ccw[24] = "";
    if (e->cc == CC_LEFT || e->cc == CC_RIGHT)
        snprintf(ccw, sizeof ccw, "G%d D%d", e->cc, e->dnum);   /* pccdia: sccomp, tloffno$ */
    else if (e->cc == CC_OFF)
        snprintf(ccw, sizeof ccw, "G40");
    words_pre = ccw[0] ? ccw : NULL;
    if (P->wpl) {   /* ours: back to XY after a G18 / G19 arc of the filter */
        words_plane = "G17";
        P->wpl = 0;
    }
    words(w, e->kind, e->p, NULL, e->kind == EV_FEED ? &e->feed : NULL, line);
    words_pre = NULL;
    words_plane = NULL;
    if (line[0])
        blk(w, line);
    memcpy(P->prev, e->p, sizeof P->prev);
}

/* An arc: split into pieces (at the quadrants for ij-quad, else of at most 180 degrees), written with R words, or I / J
   words (helix, ij-quad); tiny arcs and pieces as lines. */
static void post_arc(Post *P, const Ev *e) {
    W *w = &P->w;
    char line[256];
    double *prev = P->prev;
    int g = e->kind, u = AX[e->plane][0], v = AX[e->plane][1], dp = 3 - u - v;
    /* batch 10: an XZ / YZ arc of the Line/Arc filter (MCAM_ARC_FILTER_PLANES=1): G18 / G19 (the pst's sgplane), the
       XZ direction as the NCI writes it (the reverse of the (x, z) frame: FUN_18094ce10) - LinuxCNC G18 / G19 follow
       the same rule */
    /* dec2 (Q9): Mastercam's stored XZ / YZ arcs too (read into the same frame; before they were posted without G18 /
       G19) */
    int pg = e->plane ? (e->plane == 2 ? 18 : 19) : 17;                    /* the plane's G code */
    int gout = e->plane == 2 ? (g == EV_CW ? EV_CCW : EV_CW) : g;           /* the G2 / G3 written */
    if (pg != 17 || P->wpl) {
        if (pg != (P->wpl ? P->wpl : 17)) {
            snprintf(P->plw, sizeof P->plw, "G%d", pg);
            words_plane = P->plw;
        }
        P->wpl = pg == 17 ? 0 : pg;
    }
    double sx = prev[u], sy = prev[v], rad = hypot(sx - e->c[u], sy - e->c[v]);
    double a0 = atan2(sy - e->c[v], sx - e->c[u]), a1 = atan2(e->p[v] - e->c[v], e->p[u] - e->c[u]);
    double sw = g == 3 ? fmod(a1 - a0 + 4 * M_PI, 2 * M_PI) : fmod(a0 - a1 + 4 * M_PI, 2 * M_PI);
    int helix = fabs(e->p[dp] - prev[dp]) > 1e-9;
    if (sw < 1e-9)
        sw = 2 * M_PI;                                   /* start == end: full circle */
    if (hypot(e->p[u] - sx, e->p[v] - sy) < P->tiny && sw < M_PI) {   /* tiny arc -> straight line (as posted) */
        words(w, 1, e->p, NULL, &e->feed, line);
        if (line[0])
            blk(w, line);
        words_plane = NULL;
        memcpy(prev, e->p, sizeof P->prev);
        return;
    }
    /* the sweep at the end of each piece */
    double stops[16];
    int ns = 0;
    if (P->quad) {                                       /* break at every quadrant boundary */
        double step = M_PI / 2;
        long k = g == 3 ? (long)floor(a0 / step) + 1 : (long)ceil(a0 / step) - 1;
        for (;;) {
            double dd = g == 3 ? k * step - a0 : a0 - k * step;
            if (dd >= sw - 1e-7 || ns >= 14)
                break;
            if (dd > 1e-7)
                stops[ns++] = dd;
            k += g == 3 ? 1 : -1;
        }
        stops[ns++] = sw;
    } else {                                             /* pieces of at most 180 degrees (Mastercam's arcs) */
        /* refitted arcs: at most 120 degrees - their endpoints sit only within tol of the circle, and an R word near
           180 degrees moves the centre by ~sqrt(2 R err) (cam2d.c splits its own output the same way) */
        double lim = e->fitted ? 2 * M_PI / 3 : M_PI;
        int np = (int)ceil(sw / lim - 1e-9);
        if (np < 1)
            np = 1;
        for (int k = 1; k <= np && ns < 16; k++)
            stops[ns++] = e->fitted ? fmin(k * (sw / np), sw) : fmin(k * M_PI, sw);
    }
    /* the pieces */
    double cur[3] = { prev[0], prev[1], prev[2] };
    for (int k = 1; k <= ns; k++) {
        double done = stops[k - 1], frac = done / sw, ang = a0 + (g == 3 ? done : -done);
        double pe[3] = { e->p[0], e->p[1], e->p[2] };   /* the piece's end */
        if (k < ns) {
            pe[u] = e->c[u] + rad * cos(ang);
            pe[v] = e->c[v] + rad * sin(ang);
            pe[dp] = prev[dp] + (e->p[dp] - prev[dp]) * frac;
        }
        double psw = done - (k > 1 ? stops[k - 2] : 0);   /* the piece's sweep */
        /* arc words from the already-rounded start and centre (formats/post.md) */
        double cs = rnd(cur[u], w->dec), cv = rnd(cur[v], w->dec), ii = rnd(e->c[u], w->dec) - cs,
               jj = rnd(e->c[v], w->dec) - cv, rr = hypot(ii, jj);
        if (hypot(rnd(pe[u], w->dec) - cs, rnd(pe[v], w->dec) - cv) < P->tiny && ns > 1) {   /* tiny leftover piece ->
                                                                                                 line */
            words(w, 1, pe, NULL, &e->feed, line);
            if (line[0])
                blk(w, line);
            words_plane = NULL;
            memcpy(cur, pe, sizeof cur);
            continue;
        }
        char extra[64], a[24], bb[24];
        if (helix || P->ij) {
            fmtn(a, ii, w->dec);
            fmtn(bb, jj, w->dec);
            snprintf(extra, sizeof extra, "%c%s %c%s", "IJK"[u], a, "IJK"[v], bb);
        } else {
            fmtn(a, psw > M_PI + 1e-9 ? -rr : rr, w->dec);
            snprintf(extra, sizeof extra, "R%s", a);
        }
        words(w, gout, pe, extra, &e->feed, line);
        blk(w, line);
        words_plane = NULL;
        memcpy(cur, pe, sizeof cur);
    }
    words_plane = NULL;
    memcpy(prev, e->p, sizeof P->prev);
}

/* The program end (after the last tool): coolant off, spindle off, home, M30 / M2. */
static void post_end(Post *P) {
    W *w = &P->w;
    cool_off_all(w, &P->cmask);
    blk(w, "M5");
    blk(w, P->v9on ? "G91 G28 Z0. M9" : "G91 G28 Z0.");
    blk(w, "G28 X0. Y0.");
    blk(w, "M30");
    fprintf(P->out, "M2\n");
}

/* Posts the part F to OUT: the ops in Operations Manager order (O->only picks some), Mastercam's from their stored
   NCI, CAMOPS (made / edited here, camops[j]->op_index = their F.ops index) from their moves. Returns the count of
   operations posted (> 0); 0 + ERR when the part cannot be posted (nothing is written when a check refuses it). */
int nc_post_part(FILE *out, McamFile *f, CamOp **camops, int ncamops, const NcPostOpts *o, char *err, size_t errn) {
    NcPostOpts def = { "r180", NULL, NULL };
    if (!o)
        o = &def;
    int quad = o->style && !strcmp(o->style, "ij-quad"), ij = quad;   /* ij-quad: I/J words, quadrant pieces */
    if (!post_check_part(f, err, errn))
        return 0;
    int inch = !mcam_is_metric(f);                       /* the program's units: IsMetric() (formats/units.md) */
    const double tiny = inch ? 0.01 / MM_PER_INCH : 0.01;   /* arcs with a chord below 0.01 mm post as G1 (measured,
                                                               formats/post.md); the same length in inch */

    /* the events of every op */
    size_t len;
    uint8_t *d = post_read_part(f, &len);
    EvList ev = { 0 };
    static char tnames[MAX_TOOLS][TOOL_NAME_LEN];
    int tnums[MAX_TOOLS], ntn = 0;
    char material[128] = "";
    int nposted = post_collect_events(f, camops, ncamops, o, inch, d, len, &ev, tnames, tnums, &ntn, material,
                                      sizeof material, err, errn);
    free(d);
    if (!nposted)
        return 0;

    /* the checks, in this order (the first refusal is reported) */
    int dcyc = nc_drill_cycles_on();   /* batch 8: MCAM_DRILL_CYCLES=1 (formats/post.md "Drill cycles") */
    int ncool = nc_coolant_on();
    if (!check_values(&ev, err, errn)
        || !check_cutter_comp(f, camops, ncamops, &ev, err, errn)
        || !check_drill_cycles(&ev, dcyc, err, errn)
        || !check_drill_dwell(&ev, dcyc, err, errn)
        || !check_coolant(f, camops, ncamops, &ev, ncool, err, errn)
        || !check_tap_pitch(f, &ev, dcyc, err, errn)) {
        free(ev.e);
        return 0;
    }

    /* the program */
    Post P;
    memset(&P, 0, sizeof P);
    P.w.f = out;
    P.w.n = 100;
    P.w.dec = inch ? 4 : 3;
    P.w.fdec = inch ? 2 : 0;
    P.out = out;
    P.f = f;
    P.camops = camops;
    P.ncamops = ncamops;
    P.quad = quad;
    P.ij = ij;
    P.inch = inch;
    P.tiny = tiny;
    P.dcyc = dcyc;
    P.ncool = ncool;
    P.fortc = nc_force_tc_on();
    P.started = 0;
    P.tool = -1;
    P.last_op = -1;
    post_header(&P.w, out, f, o, inch, material, tnames, tnums, ntn);
    for (int i = 0; i < ev.n; i++) {
        const Ev *e = &ev.e[i];
        if (e->kind == EV_TOOL && e->op == P.last_op) {   /* depth-pass tool records */
            memcpy(P.prev, e->p, sizeof P.prev);
            continue;
        }
        if (e->kind == EV_TOOL)
            post_op_start(&P, e);
        else if (!P.started)
            continue;
        else if (e->kind == EV_DRILL)
            post_drill_point(&P, e);
        else if (e->kind == EV_DRILL_END)
            post_drill_end(&P);
        else if (e->kind == EV_RAPID || e->kind == EV_FEED)
            post_line(&P, e);
        else if (e->kind == EV_CW || e->kind == EV_CCW)
            post_arc(&P, e);
    }
    if (P.started)
        post_end(&P);
    free(ev.e);
    return nposted;                                       /* > 0 = the count of operations posted */
}

/* ------------------------------------------------------------------------------------------------ backplot motions */

/* The whole part file (the stored NCI is read from it at f->nci_off), *LEN bytes; NULL (*LEN = 0) when the part has
   no stored NCI or the file cannot be read. */
static uint8_t *read_part_nci(const McamFile *f, size_t *len) {
    uint8_t *d = NULL;
    *len = 0;
    if (f->path[0] && f->nci_off) {
        FILE *in = fopen(f->path, "rb");
        if (in) {
            fseek(in, 0, SEEK_END);
            long L = ftell(in);
            fseek(in, 0, SEEK_SET);
            if (L > 0) {
                *len = (size_t)L;
                d = xmalloc(*len);
                if (fread(d, 1, *len, in) != *len) {
                    free(d);
                    d = NULL;
                    *len = 0;
                }
            }
            fclose(in);
        }
    }
    return d;
}

/* A new zeroed motion at the end of *M (N in use, *CAP allocated; grows it as needed). */
static NcMotion *motion_new(NcMotion **m, int n, int *cap) {
    if (n == *cap) {
        *cap = *cap ? *cap * 2 : 1024;
        *m = xrealloc(*m, (size_t)*cap * sizeof **m);
    }
    NcMotion *q = &(*m)[n];
    memset(q, 0, sizeof *q);
    return q;
}

/* The motions the post would output, in program order (Backplot, formats/backplot.md): Mastercam's ops from their
   stored NCI, ours from their generated moves - the same event extraction as nc_post_part. ONLY: NULL or per F.ops.
   Returns the count; *OUT is malloc'd. */
int nc_motions(const McamFile *f, CamOp **camops, int ncamops, const char *only, NcMotion **out) {
    *out = NULL;
    size_t len;
    uint8_t *d = read_part_nci(f, &len);                  /* the stored NCI: the whole part file */
    static char tnames[MAX_TOOLS][TOOL_NAME_LEN];
    int tnums[MAX_TOOLS], ntn = 0;
    char material[128];
    NcMotion *m = NULL;
    int n = 0, cap = 0;
    double cur[3] = { 0, 0, 0 };                          /* where the tool is */
    int have = 0;                                         /* cur is set */
    for (int i = 0; i < f->nops; i++) {
        if (only && !only[i])
            continue;
        const CamOp *mine = NULL;
        for (int j = 0; j < ncamops; j++)
            if (camops[j]->op_index == i)
                mine = camops[j];
        EvList ev = { 0 };
        if (mine)
            camop_events(f, mine, f->ops[i].id, &ev, tnames, tnums, &ntn, 0);
        else if (f->ops[i].nci)                          /* rewritten here (Reverse toolpath, nci_reverse.c) */
            stored_events(f->ops[i].nci, f->ops[i].nci_n, 0, f->ops[i].id, &ev, tnames, tnums, &ntn, material,
                          sizeof material);
        else if (d)
            stored_events(d, len, f->nci_off, f->ops[i].id, &ev, tnames, tnums, &ntn, material, sizeof material);
        int tool = -1, rpm = 0;
        for (int k = 0; k < ev.n; k++) {
            const Ev *e = &ev.e[k];
            if (e->kind == EV_DRILL_END)
                continue;
            if (e->kind == EV_DRILL && e->has_s) {          /* FUN_67dc9540: rapid to S, rapid to R, feed to the hole,
                                                               back to S */
                double S[3] = { e->s[0], e->s[1], e->s[2] }, B[3] = { e->p[0], e->p[1], e->p[2] }, P[4][3];
                int kinds[4], np = 0;
                double dl = hypot(hypot(B[0] - S[0], B[1] - S[1]), B[2] - S[2]);
                memcpy(P[np], S, sizeof S);
                kinds[np++] = NCM_RAPID;
                if (fabs(e->rdist) > 1e-9 && dl > 0) {
                    for (int k = 0; k < 3; k++)
                        P[np][k] = S[k] + (B[k] - S[k]) / dl * e->rdist;
                    kinds[np++] = NCM_RAPID;
                }
                memcpy(P[np], B, sizeof B);
                kinds[np++] = NCM_LINE;
                memcpy(P[np], S, sizeof S);
                kinds[np++] = e->cyc == CYC_TAP ? NCM_LINE : NCM_RAPID;
                for (int s2 = 0; s2 < np; s2++) {
                    NcMotion *q = motion_new(&m, n, &cap);
                    q->op_index = i;
                    q->tool = tool;
                    q->rpm = rpm;
                    q->kind = kinds[s2];
                    q->feed = kinds[s2] == NCM_LINE ? e->feed : -1;
                    memcpy(q->a, have ? cur : P[s2], sizeof q->a);
                    memcpy(q->b, P[s2], sizeof q->b);
                    q->len = hypot(hypot(q->b[0] - q->a[0], q->b[1] - q->a[1]), q->b[2] - q->a[2]);
                    memcpy(cur, q->b, sizeof cur);
                    have = 1;
                    n++;
                }
                continue;
            }
            NcMotion *q = motion_new(&m, n, &cap);
            q->op_index = i;
            q->feed = e->feed;
            if (e->kind == EV_TOOL) {
                tool = e->tool;
                rpm = e->rpm;
                q->kind = NCM_TOOL;
            } else if (e->kind == EV_RAPID)
                q->kind = NCM_RAPID;
            else if (e->kind == EV_FEED)
                q->kind = NCM_LINE;
            else if (e->kind == EV_CW || e->kind == EV_CCW) {
                q->kind = e->kind == EV_CW ? NCM_CW : NCM_CCW;
                q->plane = e->plane;
                memcpy(q->c, e->c, sizeof q->c);
            } else if (e->kind == EV_DRILL) {
                q->kind = NCM_DRILL;
                q->rplane = e->rplane;
                q->peck = e->peck;
                q->cyc = e->cyc;
            } else
                continue;
            q->tool = tool;
            q->rpm = rpm;
            memcpy(q->a, have ? cur : e->p, sizeof q->a);
            memcpy(q->b, e->p, sizeof q->b);
            if (q->kind == NCM_DRILL)
                q->a[0] = q->b[0], q->a[1] = q->b[1], q->a[2] = e->rplane;   /* from the R plane down */
            if (q->kind == NCM_TOOL)
                memcpy(q->a, q->b, sizeof q->a);
            if (q->kind == NCM_CW || q->kind == NCM_CCW) {
                int u = q->plane == 1 ? 1 : 0, v = q->plane == 0 ? 1 : 2, w = 3 - u - v;
                double r = hypot(q->a[u] - q->c[u], q->a[v] - q->c[v]);
                double a0 = atan2(q->a[v] - q->c[v], q->a[u] - q->c[u]);
                double a1 = atan2(q->b[v] - q->c[v], q->b[u] - q->c[u]);
                double sw = q->kind == NCM_CCW ? fmod(a1 - a0 + 4 * M_PI, 2 * M_PI)
                                               : fmod(a0 - a1 + 4 * M_PI, 2 * M_PI);
                if (sw < 1e-9)
                    sw = 2 * M_PI;
                q->r = r;
                q->a0 = a0;
                q->sweep = q->kind == NCM_CCW ? sw : -sw;
                q->len = hypot(r * sw, q->b[w] - q->a[w]);
            } else
                q->len = hypot(hypot(q->b[0] - q->a[0], q->b[1] - q->a[1]), q->b[2] - q->a[2]);
            if (q->kind == NCM_DRILL)
                q->len *= 2;                                         /* down and back to R (G99) */
            memcpy(cur, q->kind == NCM_DRILL ? q->a : q->b, sizeof cur);
            have = 1;
            n++;
        }
        free(ev.e);
    }
    free(d);
    *out = m;
    return n;
}

/* point at parameter t (0..1) along motion M */
void nc_motion_point(const NcMotion *m, double t, double *p) {
    if (m->kind == NCM_CW || m->kind == NCM_CCW) {
        int u = m->plane == 1 ? 1 : 0, v = m->plane == 0 ? 1 : 2, w = 3 - u - v;
        double a = m->a0 + m->sweep * t;
        p[u] = m->c[u] + m->r * cos(a);
        p[v] = m->c[v] + m->r * sin(a);
        p[w] = m->a[w] + (m->b[w] - m->a[w]) * t;
        return;
    }
    if (m->kind == NCM_DRILL) {                                         /* down to depth, then up to R */
        double s = t < 0.5 ? t * 2 : (1 - t) * 2;
        for (int k = 0; k < 3; k++)
            p[k] = m->a[k] + (m->b[k] - m->a[k]) * s;
        return;
    }
    for (int k = 0; k < 3; k++)
        p[k] = m->a[k] + (m->b[k] - m->a[k]) * t;
}

/* ------------------------------------------------------------------------------------------------ NCI extents
   Stock Setup "NCI extents" (25077): mastercam.dll FUN_1801ef250 -> get_bnci_extents(nci, NULL, 1, 0, 0, &min, &max,
   &found) @1800fd670 -> FUN_1800fda60 (decomp/mccore_bnci_extents.c; spec: the decode notes in formats/mcam.md "Stock
   Setup"). The ops: GetOpIdsFromGroup(active machine group, 1) = every op of the group and its child groups (record
   +0x5bc = the machine group id). Mastercam's ops from their stored NCI records; ops made here from their generated
   moves with the records Mastercam would write for them: 1013 (bnci_wr_1013 @18008e540: +0x20 = op +0x2e8 = the tool's
   OverallDiameter, +0x28 = op +0x2f0 = its CornerRadius, i16 +0x30 = op_is_tip_comp == 1), the Top plane (no 1014). */

#define EXT_HUGE 1e38   /* "no value yet" for min / max / the z cap */

/* the box being grown */
typedef struct {
    double mn[3], mx[3];
    int mach;                /* machine type (1 = lathe) */
} XAcc;

/* FUN_180100750: per-axis min / max; lathe zeroes max.z, min.y, min.z */
static void xa_add(XAcc *a, const double q[3]) {
    for (int i = 0; i < 3; i++) {
        if (q[i] < a->mn[i])
            a->mn[i] = q[i];
        if (q[i] > a->mx[i])
            a->mx[i] = q[i];
    }
    if (a->mach == 1) {
        a->mx[2] = 0;
        a->mn[1] = 0;
        a->mn[2] = 0;
    }
}

/* FUN_1800fda60's running state */
typedef struct {
    XAcc a;
    double O[3];             /* 1013: the origin added to every point */
    double tm[9];            /* 1014: the view matrix */
    int hm;                  /* tm is not the identity */
    int k30;                 /* 1013 i16 +0x30 (op_is_tip_comp) */
    int active;              /* inside an active op section */
    int seen;                /* records seen (the return value) */
    int ntc;                 /* tool changes seen */
    int has_pend, has_prev;
    double h;                /* 1013 +0x20 / 2: the tool radius */
    double d28;              /* 1013 +0x28: the corner radius */
    double pend[3];          /* a rapid's end (world), added at the next feed / arc */
    double prev[3];          /* the last point (view) */
    double zcap;             /* the cap of max.z from the tool records */
    double tol, chord;
    int undec;               /* a record needed a rule that is NOT DECODED */
} XSt;

/* view_to_world(p + O, V): view < 2 identity, else M^T p */
static void xs_v2w(const XSt *s, const double p[3], double w[3]) {
    double q[3] = { p[0] + s->O[0], p[1] + s->O[1], p[2] + s->O[2] };
    if (!s->hm) {
        w[0] = q[0];
        w[1] = q[1];
        w[2] = q[2];
        return;
    }
    for (int i = 0; i < 3; i++)
        w[i] = q[0] * s->tm[i] + q[1] * s->tm[3 + i] + q[2] * s->tm[6 + i];
}

/* adds the point P lowered by DZ */
static void xs_addz(XSt *s, const double p[3], double dz) {
    double q[3] = { p[0], p[1], p[2] - dz }, w[3];
    xs_v2w(s, q, w);
    xa_add(&s->a, w);
}

/* FUN_1800fd280 at -3 / -7 (mill): max.z capped (view: Top assumed) */
static void xs_cap(XSt *s) {
    if (s->a.mach == 1)
        return;
    if (s->zcap != -EXT_HUGE && s->zcap < s->a.mx[2])
        s->a.mx[2] = s->zcap;
    s->zcap = -EXT_HUGE;
}

/* a tool change 1000-1002 [E:1656-1748] */
static void xs_tool(XSt *s, int cap_ok, double top, double depth) {
    s->has_pend = 0;
    s->ntc++;
    if (s->a.mach == 3 || !cap_ok)
        return;
    double p1[3] = { 0, 0, top }, p2[3] = { 0, 0, depth }, w1[3], w2[3];
    xs_v2w(s, p1, w1);
    xs_v2w(s, p2, w2);
    double z = w1[2] > w2[2] ? w1[2] : w2[2];
    if (z > s->zcap)
        s->zcap = z;
}

/* a rapid: its end is kept pending */
static void xs_rapid(XSt *s, const double p[3]) {
    xs_v2w(s, p, s->pend);
    s->has_pend = 1;
    memcpy(s->prev, p, sizeof s->prev);
    s->has_prev = 1;
}

/* feed point [E:1437-1648]: P, P - d28 (mill, not tip comp), P - D / 2 */
static void xs_feed(XSt *s, const double p[3]) {
    if (s->has_pend)
        xa_add(&s->a, s->pend);
    xs_addz(s, p, 0);
    if (s->a.mach == 0 && s->k30 == 0)
        xs_addz(s, p, s->d28);
    if (s->a.mach != 3)
        xs_addz(s, p, s->h);
    memcpy(s->prev, p, sizeof s->prev);
    s->has_prev = 1;
}

/* arcs 2 / 3 (plane 0) [E:932-1436]: from the previous record's point S (bnci_world_points) - BnciToEntity ->
   constr_ns_arc, then break_arc_n @1804b50f0 into n = round(|sweep| / sqrt(8 chord / r)) (FUN_1800fd4a0, 2..20) chords,
   every point at S's depth; else the fallback: the end point and the 0 / 90 / 180 / 270 degree points the arc passes */
static void xs_arc(XSt *s, int cw, int plane, int full, const double e[3], const double c[3]) {
    if (s->has_pend)
        xa_add(&s->a, s->pend);
    if (plane < 0) {
        memcpy(s->prev, e, sizeof s->prev);
        s->has_prev = 1;
        return;
    }
    if (plane != 0 || s->hm || !s->has_prev) {   /* NOT DECODED */
        s->undec = 1;
        memcpy(s->prev, e, sizeof s->prev);
        s->has_prev = 1;
        return;
    }
    const double *S = s->prev;
    double cx = c[0], cy = c[1];
    double rs = hypot(S[0] - cx, S[1] - cy);
    double r3 = sqrt((e[0] - c[0]) * (e[0] - c[0]) + (e[1] - c[1]) * (e[1] - c[1]) + (e[2] - c[2]) * (e[2] - c[2]));
    if (rs >= s->tol && !full) {
        double sa = atan2(S[1] - cy, S[0] - cx), ea = atan2(e[1] - cy, e[0] - cx), sw = cw ? sa - ea : ea - sa;
        while (sw <= 0)
            sw += 2 * M_PI;   /* constr_ns_arc NOT DECODED in full: the directed sweep S -> E */
        if (cw)
            sw = -sw;
        double step = sqrt(s->chord * 8.0 / rs);
        int ns = 2;
        if (fabs(step) > 1e-6) {
            ns = (int)floor(fabs(sw / step) + 0.5);
            if (ns > 20)
                ns = 20;
            if (ns <= 1)
                ns = 2;
        }
        for (int k = 0; k <= ns; k++) {
            double t = sa + sw * k / ns, pt[3] = { cx + rs * cos(t), cy + rs * sin(t), S[2] }, w[3];
            xs_v2w(s, pt, w);
            xa_add(&s->a, w);
        }
    } else if (rs >= s->tol && full) {
        s->undec = 1;                                      /* constr_circle's start angle NOT DECODED */
    } else {                                               /* [E:1040-1155] */
        xs_addz(s, e, 0);
        double sa = atan2(S[1] - cy, S[0] - cx) * 180 / M_PI, ea = atan2(e[1] - cy, e[0] - cx) * 180 / M_PI, sw;
        if (sa < 0)
            sa += 360;
        if (ea < 0)
            ea += 360;
        int fl = full || fabs(sa - ea) < s->tol;
        if (fl) {
            sa = 0;
            sw = 360;
        } else {
            if (sa <= ea)
                sw = cw ? -(360.0 - (ea - sa)) : ea - sa;
            else
                sw = cw ? -(sa - ea) : (360.0 - sa) + ea;
            if (sw < 0) {
                sw = fabs(sw);
                sa -= sw;
                if (sa < 0)
                    sa += 360;
            }
        }
        double tot = sw + sa, en = tot > 360 ? tot - 360 : tot;
        int wrap = en < sa;
        double qd[4][2] = { { r3, 0 }, { 0, r3 }, { -r3, 0 }, { 0, -r3 } };   /* the 0 / 90 / 180 / 270 degree points */
        int use[4] = { fl || wrap,
                       fl || (sa <= 90 && 90 < en) || (wrap && tot >= 450),
                       fl || (sa <= 180 && 180 < en) || (wrap && tot >= 540),
                       fl || (sa <= 270 && 270 < en) || (wrap && tot >= 630) };
        for (int k = 0; k < 4; k++)
            if (use[k]) {
                double pc[3] = { c[0] + qd[k][0], c[1] + qd[k][1], c[2] };
                xs_addz(s, pc, 0);
            }
    }
    memcpy(s->prev, e, sizeof s->prev);
    s->has_prev = 1;
}

/* one stored op's NCI section (records of op id `id` between its -2 and -3) */
static void xs_stored(XSt *s, const uint8_t *d, size_t len, size_t off, int id, const uint8_t *rec, uint32_t recn) {
    uint8_t b[NCI_REC_BUF];
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > NCI_REC_MAX || off + 2 + sz > len)
            break;
        memset(b, 0, sizeof b);
        memcpy(b, d + off + 2, sz < sizeof b - 1 ? sz : sizeof b - 1);
        off += 2 + sz;
        int g = rd_h(b, 0);
        if (g == NCI_OP_START) {   /* [E:790-812]: active when +0x18 != 0 (meaning NOT DECODED) */
            if (rd_i(b, 0x1c) != id)
                continue;
            if (b[0x18]) {                                 /* lathe / wire op types: not in this program's parts */
                s->active = 1;
                s->a.mach = 0;
            }
            continue;
        }
        if (rd_i(b, 4) != id)
            continue;                                      /* IdInArray(op id +0x04) [E:683] */
        if (g == NCI_OP_END) {
            xs_cap(s);
            s->active = 0;
            break;
        }
        if (g == 1040) {                                   /* skip to the matching 1041 [E:705-789] */
            while (off + 2 <= len) {
                uint16_t z2 = (uint16_t)(d[off] | d[off + 1] << 8);
                if (!z2 || off + 2 + z2 > len)
                    break;
                int g2 = (int16_t)(d[off + 2] | d[off + 3] << 8);
                off += 2 + z2;
                if (g2 == 1041)
                    break;
            }
            continue;
        }
        if (!s->active)
            continue;
        s->seen++;
        double p[3] = { rd_d(b, 0x20), rd_d(b, 0x28), rd_d(b, 0x30) };   /* bnci_get_pt @1800865c0 */
        if (g == 11) {
            p[0] = rd_d(b, 0x18);
            p[1] = rd_d(b, 0x20);
            p[2] = rd_d(b, 0x28);
        }
        if (g == 22 || g == 23)
            p[2] = rd_d(b, 0x40);
        switch (g) {
        case -7:
            xs_cap(s);
            break;
        case 1000: case 1001: case 1002: {                 /* the op record: top of stock +0x2e8 / incremental +0x2f0,
                                                              depth +0x1a0; the opcode test passes for every 2D mill
                                                              opcode (none of 9 / 10 / 15 / 44 / 107 / 131 / 132 /
                                                              multisurface) */
            int ok = rec && recn > 0x2f0 && !rec[0x2f0];
            double top = 0, dep = 0;
            if (ok) {
                memcpy(&top, rec + 0x2e8, 8);
                memcpy(&dep, rec + 0x1a0, 8);
            }
            xs_tool(s, ok, top, dep);
            break;
        }
        case 1013:                                         /* [E:1749-1846] */
            for (int k = 0; k < 3; k++) {
                double v = rd_d(b, 0x38 + 8 * k);
                s->O[k] = fabs(v) > EXT_HUGE ? 0 : v;
            }
            s->hm = 0;
            s->k30 = rd_h(b, 0x30);
            s->h = 0.5 * rd_d(b, 0x20);
            s->d28 = rd_d(b, 0x28);
            break;
        case 1014: {                                       /* find_view_m(3x3 at +0x18): Top = identity */
            static const double I[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
            for (int k = 0; k < 9; k++)
                s->tm[k] = rd_d(b, 0x18 + 8 * k);
            s->hm = 0;
            for (int k = 0; k < 9; k++)
                if (fabs(s->tm[k] - I[k]) > 1e-12)
                    s->hm = 1;
            break;
        }
        case 201: {
            double p1[3] = { rd_d(b, 0x28), rd_d(b, 0x18), 0 }, p2[3] = { rd_d(b, 0x30), rd_d(b, 0x20), 0 };
            xa_add(&s->a, p1);
            xa_add(&s->a, p2);
            break;
        }
        case 0: case 20: case 30:
            xs_rapid(s, p);
            break;
        case 1: case 11: case 21: case 22: case 23: case 31: case 81: case 100:
            xs_feed(s, p);
            break;
        case 2: case 3: {
            double c[3] = { rd_d(b, 0x38), rd_d(b, 0x40), rd_d(b, 0x48) };
            xs_arc(s, g == 2, rd_h(b, 0x18), b[0x5a], p, c);
            break;
        }
        case 32: case 33:
            s->undec = 1;                                  /* 4-axis arcs NOT DECODED */
            break;
        default:
            break;
        }
    }
}

/* The NCI extents box (nc_post.h): BOX = min x y z, max x y z; returns FUN_1800fda60's count (0 = no tool change
   seen); *UNDECODED = 1 when a record needed a rule that is NOT DECODED. */
int nc_extents(const McamFile *f, CamOp **camops, int ncamops, const int *top_inc, double tol, double chord,
               double box[6], int *undecoded) {
    XSt s;
    memset(&s, 0, sizeof s);
    for (int k = 0; k < 3; k++) {                          /* FUN_1801006d0 (p4 = 1) */
        s.a.mn[k] = EXT_HUGE;
        s.a.mx[k] = -EXT_HUGE;
    }
    s.k30 = 1;
    s.zcap = -EXT_HUGE;
    s.tol = tol;
    s.chord = chord;
    size_t len;
    uint8_t *d = read_part_nci(f, &len);                  /* the stored NCI: the whole part file */
    static char tnames[MAX_TOOLS][TOOL_NAME_LEN];
    int tnums[MAX_TOOLS], ntn = 0;
    for (int i = 0; i < f->nops; i++) {
        const Op *o = &f->ops[i];
        if (o->raw && o->raw_n > 0x5c0) {                  /* only the ops of the stock's machine group */
            int32_t g;
            memcpy(&g, o->raw + 0x5bc, 4);
            if (f->stock_rec && g != f->stock_grp)
                continue;
        }
        int j = -1;
        for (int k = 0; k < ncamops; k++)
            if (camops[k]->op_index == i)
                j = k;
        if (j >= 0) {                                      /* made / edited here: its moves as the NCI it would have */
            const CamOp *op = camops[j];
            const CamParams *p = &op->p;
            EvList ev = { 0 };
            camop_events(f, op, o->id, &ev, tnames, tnums, &ntn, 0);
            if (!ev.n) {
                free(ev.e);
                continue;
            }
            s.active = 1;                                  /* -2 */
            s.a.mach = 0;
            s.O[0] = s.O[1] = s.O[2] = 0;                  /* 1013 (bnci_wr_1013) */
            s.hm = 0;
            s.h = 0.5 * p->tool_dia;
            s.d28 = p->corner_r;
            s.k30 = p->type == CAM_DRILL && p->tip_comp == 1;   /* op_is_tip_comp @1800c7380: drill op +0x1200 (=
                                                                   tip_comp); the other opcodes' bytes are not in
                                                                   CamParams - k30 only gates the z - corner radius
                                                                   point, never lower than z - D / 2 */
            for (int k = 0; k < ev.n; k++) {
                const Ev *e = &ev.e[k];
                s.seen++;
                if (e->kind == EV_TOOL)
                    xs_tool(&s, !(top_inc && top_inc[j]), p->top_z, p->final_z);
                else if (e->kind == EV_RAPID)
                    xs_rapid(&s, e->p);
                else if (e->kind == EV_FEED || e->kind == EV_DRILL)
                    xs_feed(&s, e->p);
                else if (e->kind == EV_CW || e->kind == EV_CCW)
                    xs_arc(&s, e->kind == EV_CW, e->plane, 0, e->p, e->c);
            }
            xs_cap(&s);                                    /* -3 */
            s.active = 0;
            free(ev.e);
        } else if (o->nci)
            xs_stored(&s, o->nci, o->nci_n, 0, o->id, o->raw, o->raw_n);
        else if (d)
            xs_stored(&s, d, len, f->nci_off, o->id, o->raw, o->raw_n);
    }
    free(d);
    XAcc *a = &s.a;                                        /* [E:2080-2112] */
    if (fabs(a->mn[0] - a->mx[0]) <= tol) {
        a->mn[0] -= 2 * tol;
        a->mx[0] += 2 * tol;
    }
    if (fabs(a->mn[1] - a->mx[1]) <= 2 * tol) {
        a->mn[1] -= 2 * tol;
        a->mx[1] += 2 * tol;
    }
    if (a->mach == 0 && fabs(a->mn[2] - a->mx[2]) <= 2 * tol) {
        a->mn[2] -= 2 * tol;
        a->mx[2] += 2 * tol;
    }
    for (int k = 0; k < 3; k++) {
        box[k] = a->mn[k];
        box[3 + k] = a->mx[k];
    }
    if (undecoded)
        *undecoded = s.undec;
    return s.ntc > 0 ? s.seen : 0;
}
