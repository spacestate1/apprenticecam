/* nc_post.c - post a whole part to G-code in the format of the user's own Mastercam post (LinuxCNC 4020):
 * Mastercam's operations from the binary NCI stored in the part (exactly what Mastercam computed), operations made or
 * edited here from their generated moves. Port of ../tools/mcam_post.py (validated: BRACKET4 matches Mastercam's
 * posted program block for block); rules in ../formats/post.md. This is not an MP (.pst) interpreter. */
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

/* ------------------------------------------------------------------------------------------------ events */
enum { EV_RAPID = 0, EV_FEED = 1, EV_CW = 2, EV_CCW = 3, EV_DRILL_END = 80, EV_DRILL = 81, EV_TOOL = 1000 };
typedef struct {
    int kind;
    double p[3];             /* end point (world) / drill point (x, y, depth) / tool change: first position */
    double c[3];             /* arc centre (world; the depth axis is unused) */
    int plane;               /* arcs: 0 XY, 1 YZ, 2 XZ (Mastercam NCI planes) */
    double feed, rplane, peck;
    int cyc;                 /* drill: Mastercam cycle (1 Peck Drill G83, 2 Chip Break G73) */
    double dwell;            /* drill: NCI 81 +0x38 (bnci_wr_81: per-point override or record 0x1250) / CamParams.drill_dwell (batch 8) */
    int tool, rpm, op;
    int fitted;              /* arcs refitted from our generated moves (endpoints only within tolerance of the circle) */
    int geo;                 /* batch 10: an arc of generated moves - kind is its direction in the plane's (u, v) frame; posted XZ arcs
                                take the NCI / G18 direction, the reverse (mccore FUN_180944830 / FUN_18094ce10 flip plane 2) */
    int has_s; double s[3], rdist;   /* NCI 81: start point (+0x88) and R distance (+0x68 - +0x70) - backplot expansion */
    int cc, ccst, dnum;      /* cutter compensation code on this move (41 / 42 start, 140 end; Move.cc / the NCI cc field), from a
                                stored NCI (ccst), the D word (dnum: the tool number, dec2 Q30) */
} Ev;
/* decisions batch dec2 (Q29): the op's compensation type (0 Computer, 1 Control, 2 Wear, 3 Reverse wear, 4 Off): Contour record
   0x5d0 (CamParams.ct_comp), the Pocket finish pass 0x13a8 (cam2d_pock_fin.c), the other types 0x5d0 (CamParams.mc_comp) */
static int op_cc_type(const CamParams *p) {
    if (p->type == CAM_CONTOUR) return p->ct_comp;
    if (p->type == CAM_POCKET) { int16_t v = 0; if (p->pk_ok) memcpy(&v, p->pk_p + (0x13a8 - 0x1228), 2); return v; }
    return p->mc_comp;
}
int nc_cutter_comp_on(void) { const char *e = getenv("MCAM_CUTTER_COMP"); return e && !strcmp(e, "1"); }
int nc_drill_cycles_on(void) { const char *e = getenv("MCAM_DRILL_CYCLES"); return e && !strcmp(e, "1"); }
int nc_coolant_on(void) { const char *e = getenv("MCAM_COOLANT"); return e && !strcmp(e, "1"); }
int nc_force_tc_on(void) { const char *e = getenv("MCAM_FORCE_TOOL_CHANGE"); return e && !strcmp(e, "1"); }

/* ------------------------------------------------------------------------------------------------ coolant (batch 11, MCAM_COOLANT=1)
   What Mastercam passes (cam_postvals.c): X style (the machine's UseCoolantFromPost off - the user's BRACKET4 / A001 / ... machines) =
   coolant entries in the op's canned text array (record 0xb52, NCI 1025 at the section start): option k on / off, before / with /
   after; V9 style (UseCoolantFromPost on - test-cube-10mm's) = the op's coolant word (record 0x398, NCI 1000-1002 +0x48: 0x10 flood,
   0x20 mist, 0x40 thru, 8 off). How the stock post the parts name (Generic Fanuc 3X Mill.pst, shared Mcam2017 mill/Posts) writes them:
   X style - pcan l.1404 ("before": own blocks ahead of "T M6" in ptlchg$ l.736, ahead of the spindle in ptlchg0$), pcan1_cool l.1461
   via pscool l.1003 ("with": on the "G43 H Z" line, ptlchg$ l.753; own block in ptlchg0$ l.716), pcan2 l.1486 ("after": own blocks after
   the tool change, pcom_movea l.966), pcant_out l.1614 (an on code is suppressed while that coolant is on; an off code is written only
   while some coolant is on), pretract l.770 (at each tool change / end: the off code of every coolant still on, before "M5"); codes
   scool50 / 51 = M8 / M9 (coolant 1, Flood), scool52 / 53 = M7 / M9 (coolant 2, Mist) l.407-410 - LinuxCNC's M7 mist / M8 flood /
   M9 off. V9 style - pscool's scoolant on the "G43" line (sm08* l.395-398), M9 at pretract's "G91 G28 Z0." line. Thru-tool (M88 /
   M89) and custom options 4-10 as the stock post (2026-11-04; refused before); V9 thru = sm08_2 "M8"; V9 mist is written M7 (Q13) (the stock V9
   table writes M8 for every option); V9 M9 only after an op that turned coolant on; non-coolant canned text is not posted (as before). */
typedef struct { int n, v9; struct { int8_t k, on, pos; } e[20]; } OpCool;
static int16_t rec16(const uint8_t *r, uint32_t n, const CamParams *p, int off) {   /* record field with the op's patches over it */
    int16_t v = 0;
    if (r && (uint32_t)off + 2 <= n) memcpy(&v, r + off, 2);
    if (p) for (int k = 0; k < p->nrpatch; k++) if (p->rpatch[k].off == off && p->rpatch[k].n == 2) memcpy(&v, p->rpatch[k].v, 2);
    return v;
}
static void op_coolant(const McamFile *f, CamOp **camops, int ncamops, int opid, OpCool *c) {
    memset(c, 0, sizeof *c);
    const CamOp *mine = NULL; const Op *so = NULL;
    for (int k = 0; k < f->nops; k++) if (f->ops[k].id == opid) so = &f->ops[k];
    for (int j = 0; j < ncamops; j++) if (camops[j]->op_index >= 0 && camops[j]->op_index < f->nops && f->ops[camops[j]->op_index].id == opid) mine = camops[j];
    const uint8_t *r = NULL; uint32_t n = 0;
    if (so && so->raw) { r = so->raw; n = so->raw_n; }
    if (mine && !(so && so->raw && mine->p.file_op_id > 0)) r = cam_default_op_record(mine->p.type, mcam_is_metric(f), &n);   /* an op made here: its defaults op */
    const CamParams *p = mine ? &mine->p : NULL;
    if (pv_cool_frompost(pv_machine(f))) {
        int16_t w = rec16(r, n, NULL, PV_COOL_OLD), patched = 0;
        if (p) for (int k = 0; k < p->nrpatch; k++) if (p->rpatch[k].off == PV_COOL_OLD && p->rpatch[k].n == 2) { memcpy(&w, p->rpatch[k].v, 2); patched = 1; }
        if (p && !patched) {                                 /* mcam_saveops.c: the op's tool block +0x50 (TlCoolant.OldStyle), 16 / 8 by CamParams.coolant */
            const Tool *tl = mcam_tool(f, p->tool_num); int st = tl ? tl->coolant_style : 0;
            w = p->coolant ? 0x10 : (st == 0x20 || st == 0x40) ? st : (mine->p.file_op_id > 0 && w != 0x10) ? w : 8;
        }
        c->v9 = w == 0x10 ? 1 : w == 0x20 ? 2 : w == 0x40 ? 3 : 0;
        return;
    }
    int on = r && n > PV_CAN_ON ? r[PV_CAN_ON] : 0;
    if (p) for (int k = 0; k < p->nrpatch; k++) if (p->rpatch[k].off == PV_CAN_ON && p->rpatch[k].n == 1) on = p->rpatch[k].v[0];
    if (!on) return;                                         /* bnci_wr_1025(op, op[0xae8] = 0): no entries */
    for (int i = 0; i < 20; i++) {
        int v = rec16(r, n, p, PV_CAN + 2 * i), rm = v % 1000, a = v < 0 ? -v : v;
        if (!v) continue;
        if (rm > -101 || rm < -120) continue;                /* canned text (not coolant): not posted */
        int code = -rm; c->e[c->n].k = (int8_t)((code - 101) / 2); c->e[c->n].on = (code - 101) % 2 == 0; c->e[c->n].pos = (int8_t)(a / 1000 - 3); c->n++;
    }
}
/* batch 12 (MCAM_FORCE_TOOL_CHANGE=1): the Tool page's Force tool change 25196 = op +0x289 = record 0x2f1 (ToolPageBase::MapToOp
   0x180886e20; an op made here: its record patch, tp_apply). Mastercam's Post pre-pass AssignToolChanges (mcmill FUN_180037ca0, ported
   as viewer/mp/nci_ascii.c assign_tool_changes; formats/mp_engine.md "CBnciPost pre-pass") gives an op that starts with the previous
   op's tool number gcode 1002 (a full tool change) when its +0x289 is set (and the op is not op_is_notool / wire), else 1000 (null
   tool change); the stock post's ptlchg$ then writes the retract + "T M6" block as for another tool. */
static int op_force_tc(const McamFile *f, CamOp **camops, int ncamops, int opid) {
    const CamOp *mine = NULL; const Op *so = NULL;
    for (int k = 0; k < f->nops; k++) if (f->ops[k].id == opid) so = &f->ops[k];
    for (int j = 0; j < ncamops; j++) if (camops[j]->op_index >= 0 && camops[j]->op_index < f->nops && f->ops[camops[j]->op_index].id == opid) mine = camops[j];
    const uint8_t *r = NULL; uint32_t n = 0;
    if (so && so->raw) { r = so->raw; n = so->raw_n; }
    if (mine && !(so && so->raw && mine->p.file_op_id > 0)) r = cam_default_op_record(mine->p.type, mcam_is_metric(f), &n);   /* as op_coolant */
    int v = r && n > 0x2f1 ? r[0x2f1] : 0;
    if (mine) for (int k = 0; k < mine->p.nrpatch; k++) if (mine->p.rpatch[k].off == 0x2f1 && mine->p.rpatch[k].n == 1) v = mine->p.rpatch[k].v[0];
    return v != 0;
}
/* the X-style codes: scool50..69 (Generic Fanuc 3X Mill.pst l.407-426) - coolant 1 M8 / M9, 2 M7 / M9, 3 (thru-tool) M88 / M89,
   4-10 "M8(CoolantN=ON)" / "M9(CoolantN=OFF)". 2026-11-04 the user's rule (user_decisions.md): as the stock post - thru-tool and
   the custom coolants were refused before (Q13); LinuxCNC itself has no M88 / M89 (its own M-codes stop at M7 / M8 / M9) */
static const char *cool_code(int k, int on) {
    static const char *const tab[10][2] = { { "M9", "M8" }, { "M9", "M7" }, { "M89", "M88" },
        { "M9(Coolant4=OFF)", "M8(Coolant4=ON)" }, { "M9(Coolant5=OFF)", "M8(Coolant5=ON)" }, { "M9(Coolant6=OFF)", "M8(Coolant6=ON)" },
        { "M9(Coolant7=OFF)", "M8(Coolant7=ON)" }, { "M9(Coolant8=OFF)", "M8(Coolant8=ON)" }, { "M9(Coolant9=OFF)", "M8(Coolant9=ON)" },
        { "M9(Coolant10=OFF)", "M8(Coolant10=ON)" } };
    return k >= 0 && k < 10 ? tab[k][on ? 1 : 0] : NULL;
}

/* ------------------------------------------------------------------------------------------------ drill post text (batch 8)
   DrillCyclePageBase (decomp/mcmill_all.c l.444402 EnableControls, InitPage l.444621) labels its controls from the post text page
   0x27 entry cycle x 12 + k (k 1 cycle name, 3 Dwell, 7 1st peck, 8 Subsequent peck, 9 Peck clearance, 10 Retract amount, 11 Shift)
   through CPostText::SetDlgControlTextFromIndex (decomp/mcpost_parse.c l.8356): no text in the post -> the default (mccoreres
   7645 + 11 cycle + k - 1, CPostText::ManageTextDrill l.5214) and enabled; "" -> the default and DISABLED; any other text -> that
   text and enabled. The post text: the [CTRL_MILL|GENERIC FANUC 3X MILL] pages of Generic Fanuc 3X Mill.pst (shared mcamx9/mill/
   Posts, l.1868-1931; [CTRL_MILL|DEFAULT] l.1792-1855 holds the same), the post the user's parts name - BRACKET4.mcx-9 embeds the same
   [CTRL_MILL|DEFAULT] text (file offset 585130). Ours: the part's own embedded control text is not read (its control page is
   compressed, NOT DECODED); this table stands in for it. */
static const struct { int cyc, k; const char *s; } DRILL_PT[] = {
    { 0, 1, "Drill/Counterbore" }, { 0, 7, "" }, { 0, 8, "" }, { 0, 9, "" }, { 0, 10, "" }, { 0, 11, "" },
    { 1, 3, "" }, { 1, 7, "Peck" }, { 1, 8, "" }, { 1, 9, "" }, { 1, 10, "" }, { 1, 11, "" },
    { 2, 3, "" }, { 2, 7, "Peck" }, { 2, 8, "" }, { 2, 9, "" }, { 2, 10, "" }, { 2, 11, "" },
    { 3, 3, "" }, { 3, 7, "" }, { 3, 8, "" }, { 3, 9, "" }, { 3, 10, "" }, { 3, 11, "" },
    { 4, 1, "Bore #1 (feed-out)" }, { 4, 7, "" }, { 4, 8, "" }, { 4, 9, "" }, { 4, 10, "" }, { 4, 11, "" },
    { 5, 1, "Bore #2 (stop spindle, rapid out)" }, { 5, 3, "" }, { 5, 7, "" }, { 5, 8, "" }, { 5, 9, "" }, { 5, 10, "" }, { 5, 11, "" },
    { 6, 1, "Fine Bore (shift)" }, { 6, 7, "" }, { 6, 8, "" }, { 6, 9, "" }, { 6, 10, "" },
    { 7, 1, "Rigid Tapping Cycle" }, { 7, 3, "" }, { 7, 7, "" }, { 7, 8, "" }, { 7, 9, "" }, { 7, 10, "" }, { 7, 11, "" },
};
const char *nc_drill_text(int cyc, int k, int *on) {
    static const char *const NAME[20] = { "Drill/Counterbore", "Peck Drill", "Chip Break", "Tap", "Bore #1", "Bore #2", "Misc. #1", "Misc. #2",
        "Custom cycle 9", "Custom cycle 10", "Custom Cycle 11", "Custom Cycle 12", "Custom Cycle 13", "Custom Cycle 14", "Custom Cycle 15",
        "Custom Cycle 16", "Custom Cycle 17", "Custom Cycle 18", "Custom Cycle 19", "Custom Cycle 20" };   /* mccoreres 7645 + 11 n */
    static const char *const ROW[12] = { "", "", "Feed rate", "Dwell", "Clearance...", "Retract...", "Depth...", "First peck",
        "Subsequent peck", "Peck clearance", "Retract amount", "Shift" };                                  /* mccoreres 7646.. 7655 */
    if (on) *on = 1;
    if (cyc < 0 || cyc > 19 || k < 1 || k > 11) return "";
    const char *def = k == 1 ? NAME[cyc] : ROW[k];
    for (int i = 0; i < (int)(sizeof DRILL_PT / sizeof DRILL_PT[0]); i++)
        if (DRILL_PT[i].cyc == cyc && DRILL_PT[i].k == k) { if (!DRILL_PT[i].s[0]) { if (on) *on = 0; return def; } return DRILL_PT[i].s; }
    return def;
}
typedef struct { Ev *e; int n, cap; } EvList;
static void ev_push(EvList *l, Ev e) { if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 1024; l->e = xrealloc(l->e, l->cap * sizeof(Ev)); } l->e[l->n++] = e; }
static const int AX[3][2] = { { 0, 1 }, { 1, 2 }, { 0, 2 } };

static double rd_d(const uint8_t *b, int o) { double v; memcpy(&v, b + o, 8); return v; }
static int rd_i(const uint8_t *b, int o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static int rd_h(const uint8_t *b, int o) { int16_t v; memcpy(&v, b + o, 2); return v; }

/* the NCI section of one stored op as events; tool name (20001) / material (20016) texts are returned too */
static void stored_events(const uint8_t *d, size_t len, size_t off, int op_id, EvList *out, char tnames[][80], int *tnums, int *ntn, char *material, size_t mn) {
    uint8_t buf[0x371]; int in = 0; char tname[80] = "";      /* one byte more than copied: texts always end */
    double drill_pz = 0;                                     /* the drill cycle's point z (from its 81 record) */
    double cur[3] = { 0, 0, 0 };
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        memset(buf, 0, sizeof buf); memcpy(buf, d + off + 2, sz < sizeof buf - 1 ? sz : sizeof buf - 1);
        off += 2 + sz;
        int g = rd_h(buf, 0);
        if (g == -2) { in = rd_i(buf, 0x1c) == op_id; continue; }
        if (!in) continue;
        if (g == -3) { in = 0; continue; }
        Ev e; memset(&e, 0, sizeof e); e.op = op_id;
        if (g == 20001) {                                    /* tool name (leading spaces kept, trailing removed) */
            if (buf[0x18] != 0xfe) { snprintf(tname, sizeof tname, "%s", (const char *)buf + 0x18); size_t L = strlen(tname); while (L && tname[L - 1] == ' ') tname[--L] = 0; }
        } else if (g == 20016) {
            if (buf[0x18] && buf[0x18] != 0xfe && material) { snprintf(material, mn, "%s", (const char *)buf + 0x18); }
        } else if (g >= 1000 && g <= 1002) {
            e.kind = EV_TOOL; e.tool = rd_i(buf, 0x24); e.rpm = rd_i(buf, 0x38); e.feed = rd_d(buf, 0x40);
            for (int k = 0; k < 3; k++) e.p[k] = cur[k] = rd_d(buf, 0x50 + 8 * k);
            int seen = 0; for (int k = 0; k < *ntn; k++) if (tnums[k] == e.tool) seen = 1;
            if (!seen && *ntn < 256) { tnums[*ntn] = e.tool; snprintf(tnames[*ntn], 80, "%s", tname); (*ntn)++; }
            ev_push(out, e);
        } else if (g == 0 || g == 1) {
            double feed = rd_d(buf, 0x38);
            e.kind = feed < 0 ? EV_RAPID : EV_FEED; e.feed = feed;
            { int cc = rd_h(buf, 0x18); if (cc == 41 || cc == 42 || cc == 140) { e.cc = cc; e.ccst = 1; } }   /* NCI cc (formats/mcam.md G0/G1 +0x18) */
            for (int k = 0; k < 3; k++) e.p[k] = cur[k] = rd_d(buf, 0x20 + 8 * k);
            ev_push(out, e);
        } else if (g == 2 || g == 3) {
            int pl = rd_h(buf, 0x18); if (pl < 0 || pl > 2) pl = 0;
            int u = AX[pl][0], v = AX[pl][1], dp = 3 - u - v;
            e.kind = g; e.plane = pl; e.feed = rd_d(buf, 0x50);
            { int cc = rd_h(buf, 0x1a); if (cc == 41 || cc == 42 || cc == 140) { e.cc = cc; e.ccst = 1; } }   /* G2 / G3 +0x1a */
            e.p[u] = rd_d(buf, 0x20); e.p[v] = rd_d(buf, 0x28); e.p[dp] = rd_d(buf, 0x30);
            e.c[u] = rd_d(buf, 0x38); e.c[v] = rd_d(buf, 0x40); e.c[dp] = cur[dp];
            /* decisions batch dec2 (Q9): a plane-2 (XZ) arc is in view 2 (mccore FUN_18094b400): its depth +0x30 is -y
               (view_to_world, mccore FUN_180944830 l.355 in decomp/mccore_filter.c; checked: 403 / 403 STEEPSHALLOW arcs meet the
               previous line's y only so) and its G2 / G3 code is the reverse of the direction in the (x, z) frame (FUN_180944830
               l.346-352 swaps 2 / 3 for view 2) - kept here as the (x, z)-frame direction, as the filter's own arcs (geo) */
            if (pl == 2) { e.p[dp] = -e.p[dp]; e.kind = g == 2 ? EV_CCW : EV_CW; }
            for (int k = 0; k < 3; k++) cur[k] = e.p[k];
            ev_push(out, e);
        } else if (g == 81 || g == 100) {                    /* drill point: 81 first of a cycle, 100 each further one */
            e.kind = EV_DRILL; e.p[0] = rd_d(buf, 0x20); e.p[1] = rd_d(buf, 0x28); e.p[2] = rd_d(buf, 0x30);
            e.feed = rd_d(buf, g == 81 ? 0x40 : 0x50);
            /* bnci_wr_81 / bnci_wr_100 (formats/mcmill_2d_engines.md 5.3-5.4): +0x68 / +0x38 = initial - point z,
               +0x70 / +0x40 = R plane - point z; point z = initial point z (+0x98) - (+0x68) from the cycle's 81 record */
            if (g == 81) drill_pz = rd_d(buf, 0x98) - rd_d(buf, 0x68);
            e.rplane = drill_pz + rd_d(buf, g == 81 ? 0x70 : 0x40);
            if (g == 81) {                                   /* bnci_wr_81 (mccore): +0x18 cycle (op +0x1228), +0x48 1st peck */
                e.cyc = rd_h(buf, 0x18); e.peck = e.cyc == 1 || e.cyc == 2 ? rd_d(buf, 0x48) : 0;
                e.dwell = rd_d(buf, 0x38);                   /* batch 8: dwell (formats/mcmill_2d_engines.md 5.3) */
                /* backplotbnci E_nci_mill_81 = this record - 8 (cycle +0x10, hole +0x18, feed +0x38, R +0x60 / +0x68, S +0x80) */
                e.has_s = 1; e.s[0] = rd_d(buf, 0x88); e.s[1] = rd_d(buf, 0x90); e.s[2] = rd_d(buf, 0x98); e.rdist = rd_d(buf, 0x68) - rd_d(buf, 0x70);
            }
            cur[0] = e.p[0]; cur[1] = e.p[1]; cur[2] = e.rplane;
            ev_push(out, e);
        } else if (g == 80) { e.kind = EV_DRILL_END; ev_push(out, e); }
    }
}

/* ------------------------------------------------------------------------------------------------ fitting arcs into
   generated moves (ops made here): longest run of points on one circle (XY), within tol */
static int circ3(const double *a, const double *b, const double *c, double *cx, double *cy, double *r) {
    double d = 2 * (a[0] * (b[1] - c[1]) + b[0] * (c[1] - a[1]) + c[0] * (a[1] - b[1]));
    if (fabs(d) < 1e-12) return 0;
    double a2 = a[0] * a[0] + a[1] * a[1], b2 = b[0] * b[0] + b[1] * b[1], c2 = c[0] * c[0] + c[1] * c[1];
    *cx = (a2 * (b[1] - c[1]) + b2 * (c[1] - a[1]) + c2 * (a[1] - b[1])) / d;
    *cy = (a2 * (c[0] - b[0]) + b2 * (a[0] - c[0]) + c2 * (b[0] - a[0])) / d;
    *r = hypot(a[0] - *cx, a[1] - *cy);
    return 1;
}
static int fit_arc(const Move *m, int i0, int n, double tol, double *cx, double *cy, int *ccw) {
    int best = -1;
    if (m[i0].cc || (i0 + 1 < n && m[i0 + 1].cc)) return -1;   /* a move carrying a cutter compensation code stays a line of its own */
    if (i0 + 1 < n && (m[i0 + 1].rapid || fabs(m[i0 + 1].feed - m[i0].feed) > 1e-9)) return -1;   /* the 2nd point too */
    for (int k = i0 + 2; k < n && k < i0 + 720; k++) {
        if (m[k].rapid || fabs(m[k].feed - m[i0].feed) > 1e-9 || m[k].cc) break;
        double a[2] = { m[i0 - 1].p.x, m[i0 - 1].p.y }, b[2] = { m[(i0 - 1 + k) / 2].p.x, m[(i0 - 1 + k) / 2].p.y }, c[2] = { m[k].p.x, m[k].p.y };
        double x, y, r;
        if (!circ3(a, b, c, &x, &y, &r) || r > 5000 || r < tol * 4) break;
        int ok = 1, dir = 0; double sw = 0, acc[730];
        for (int j = i0; j <= k && ok; j++) {
            if (fabs(hypot(m[j].p.x - x, m[j].p.y - y) - r) > tol) ok = 0;
            double ax = m[j - 1].p.x - x, ay = m[j - 1].p.y - y, bx = m[j].p.x - x, by = m[j].p.y - y;
            double cr = ax * by - ay * bx, ang = atan2(cr, ax * bx + ay * by);
            int s2 = cr > 0 ? 1 : -1;
            if (fabs(ang) > M_PI / 4) ok = 0;
            if (!dir) dir = s2; else if (s2 != dir) ok = 0;
            sw += fabs(ang); acc[j - i0] = sw;
            if (r - sqrt(fmax(0, r * r - pow(hypot(bx - ax, by - ay) / 2, 2))) > tol) ok = 0;
        }
        if (ok) { double z0 = m[i0 - 1].p.z, dz = m[k].p.z - z0;
            for (int j = i0; j <= k && ok; j++) if (fabs(z0 + dz * acc[j - i0] / sw - m[j].p.z) > tol) ok = 0; }
        if (!ok || sw > 2 * M_PI - 1e-6) break;
        best = k; *cx = x; *cy = y; *ccw = dir > 0;
    }
    return best;
}

static void camop_events_at(const McamFile *f, const CamOp *op, int op_id, EvList *out, char tnames[][80], int *tnums, int *ntn);
/* ops made here keep their moves in world coordinates (the display's); the post writes them in op coordinates, the NCI's
   frame (wpt_to_op, mccore 0x1800a5950: the Tplane's rows . (p - origin); cam2d.c cam_plane_w2v). The Top plane at the world
   origin is the identity: nothing is mapped (every existing program stays as it was). to_op = 0: world (Backplot, Verify) */
static void camop_events(const McamFile *f, const CamOp *op, int op_id, EvList *out, char tnames[][80], int *tnums, int *ntn, int to_op) {
    if (!to_op || cam_op_top(&op->p)) { camop_events_at(f, op, op_id, out, tnames, tnums, ntn); return; }
    CamOp c = *op;
    c.m = malloc((op->nm ? op->nm : 1) * sizeof(Move)); memcpy(c.m, op->m, op->nm * sizeof(Move));
    for (int i = 0; i < c.nm; i++) { double w[3] = { c.m[i].p.x, c.m[i].p.y, c.m[i].p.z }, v[3]; cam_plane_w2v(op->p.tplane, op->p.torg, w, v); c.m[i].p.x = v[0]; c.m[i].p.y = v[1]; c.m[i].p.z = v[2]; }
    Hole *hv = NULL;
    if (op->nholes) { hv = malloc(op->nholes * sizeof(Hole)); memcpy(hv, op->holes, op->nholes * sizeof(Hole));
        for (int i = 0; i < op->nholes; i++) { double w[3] = { hv[i].x, hv[i].y, 0 }, v[3]; cam_plane_w2v(op->p.tplane, op->p.torg, w, v); hv[i].x = v[0]; hv[i].y = v[1]; }
        c.holes = hv; }
    camop_events_at(f, &c, op_id, out, tnames, tnums, ntn);
    free(c.m); free(hv);
}
static void camop_events_at(const McamFile *f, const CamOp *op, int op_id, EvList *out, char tnames[][80], int *tnums, int *ntn) {
    const CamParams *p = &op->p;
    if (!op->nm) return;
    Ev e; memset(&e, 0, sizeof e); e.op = op_id;
    e.kind = EV_TOOL; e.tool = p->tool_num; e.rpm = p->rpm; e.p[0] = op->m[0].p.x; e.p[1] = op->m[0].p.y; e.p[2] = op->m[0].p.z;
    ev_push(out, e);
    int seen = 0; for (int k = 0; k < *ntn; k++) if (tnums[k] == p->tool_num) seen = 1;
    if (!seen && *ntn < 256) { const Tool *t = mcam_tool(f, p->tool_num); tnums[*ntn] = p->tool_num; snprintf(tnames[*ntn], 80, "%s", t ? t->name : ""); (*ntn)++; }
    if (p->type == CAM_DRILL) {
        for (int h = 0; h < op->nholes; h++) {
            memset(&e, 0, sizeof e); e.op = op_id; e.kind = EV_DRILL;
            e.p[0] = op->holes[h].x; e.p[1] = op->holes[h].y; e.p[2] = cam_drill_z(p); e.feed = p->plunge_feed; e.rplane = p->r_plane; e.peck = p->drill_cycle == 1 || p->drill_cycle == 2 ? p->peck : 0; e.cyc = p->drill_cycle;
            e.dwell = p->drill_dwell;
            ev_push(out, e);
        }
        memset(&e, 0, sizeof e); e.op = op_id; e.kind = EV_DRILL_END; ev_push(out, e);
        memset(&e, 0, sizeof e); e.op = op_id; e.kind = EV_RAPID; e.feed = -1;
        e.p[0] = op->holes[op->nholes - 1].x; e.p[1] = op->holes[op->nholes - 1].y; e.p[2] = p->clearance; ev_push(out, e);
        return;
    }
    const Move *m = op->m; int n = op->nm;
    for (int k = 1; k < n; k++) {
        if (p->arcs && !m[k].rapid && m[k].arc > 0) {   /* an engine arc record (cam2d.c mb_tag_arc): its own centre */
            int en = k;
            while (en + 1 < n && m[en + 1].arc == m[k].arc && !m[en + 1].rapid) en++;
            memset(&e, 0, sizeof e); e.op = op_id; e.kind = m[k].cw ? EV_CW : EV_CCW; e.plane = m[k].apl; e.feed = m[k].feed; e.fitted = 0; e.geo = 1;
            e.p[0] = m[en].p.x; e.p[1] = m[en].p.y; e.p[2] = m[en].p.z; e.c[0] = m[k].cx; e.c[1] = m[k].cy; e.c[2] = m[k].apl ? m[k].cz : m[k - 1].p.z;
            for (int j = k; j <= en; j++) if (m[j].cc) e.cc = m[j].cc;   /* batch 7: a code on an arc is refused below, never dropped */
            ev_push(out, e); k = en; continue;
        }
        if (p->arcs && !m[k].rapid && !op->filtered) {   /* batch 10: a filtered op's lines stay lines (Mastercam's filter output) */
            double cx = 0, cy = 0; int ccw = 0;
            int en = fit_arc(m, k, n, fmax(0.003, p->tol * 1.2), &cx, &cy, &ccw);
            if (en >= k + 2) {
                memset(&e, 0, sizeof e); e.op = op_id; e.kind = ccw ? EV_CCW : EV_CW; e.plane = 0; e.feed = m[k].feed; e.fitted = 1;
                e.p[0] = m[en].p.x; e.p[1] = m[en].p.y; e.p[2] = m[en].p.z; e.c[0] = cx; e.c[1] = cy; e.c[2] = m[k - 1].p.z;
                ev_push(out, e); k = en; continue;
            }
        }
        memset(&e, 0, sizeof e); e.op = op_id; e.kind = m[k].rapid ? EV_RAPID : EV_FEED; e.feed = m[k].rapid ? -1 : m[k].feed;
        e.p[0] = m[k].p.x; e.p[1] = m[k].p.y; e.p[2] = m[k].p.z;
        e.cc = m[k].cc;
        if (e.cc) e.dnum = p->tool_num;   /* decisions batch dec2 (Q30, ours for LinuxCNC): D<tool number> - LinuxCNC's D word selects a
                                                    tool and takes its table diameter (interp_convert.cc convert_cutter_compensation_on); Mastercam's
                                                    pst writes the diameter offset number (tloffno$, TL +0x28 = op record 0x370), which equals the
                                                    tool number in all 205 user ops, so the output is the same for them (formats/post.md) */
        ev_push(out, e);
    }
}

/* ------------------------------------------------------------------------------------------------ output */
typedef struct {
    FILE *f; int n;
    char G[8], X[24], Y[24], Z[24], F[24]; int S, have_S, cycle, tapping;
    int dec, fdec;
} W;
static double rnd(double v, int dec) { double s = pow(10, dec), r = floor(fabs(v) * s + 0.5 + 1e-9) / s; return v < 0 && r != 0 ? -r : r; }
static void fmtn(char *o, double v, int dec) {          /* half-up, no trailing zeros: "15.", ".299", "-1.2" */
    char t[48]; snprintf(t, sizeof t, "%.*f", dec, rnd(v, dec));
    if (dec > 0) { char *e = t + strlen(t) - 1; while (*e == '0') *e-- = 0; }
    else strcat(t, ".");
    char *q = t; if (!strncmp(q, "0.", 2)) q++; else if (!strncmp(q, "-0.", 3)) { q[1] = '-'; q++; }
    if (!strcmp(q, "-.") || !strcmp(q, ".")) q = "0.";
    snprintf(o, 24, "%s", q);                            /* every caller's buffer is char[24]; values are range-checked first */
}
static void blk(W *w, const char *s) { fprintf(w->f, "N%d %s\n", w->n, s); w->n = w->n < 9998 ? w->n + 2 : 100; }
/* batch 11 (MCAM_COOLANT=1): the X-style coolant entries of position POS (0 before, 1 with, 2 after) - pcant_out (Generic Fanuc 3X
   Mill.pst l.1614): an on code only while that coolant is off ("with": appended to APP, else its own block), an off code only while
   some coolant is on (always its own block) */
static void cool_pos(W *w, const OpCool *c, int pos, int *mask, char *app, size_t an) {
    for (int i = 0; i < c->n; i++) {
        if (c->e[i].pos != pos) continue;
        int k = c->e[i].k, bit = 1 << k; const char *cd = cool_code(k, c->e[i].on); if (!cd) continue;
        if (c->e[i].on) { if (*mask & bit) continue; *mask |= bit;
            if (app && pos == 1) snprintf(app + strlen(app), an - strlen(app), " %s", cd); else blk(w, cd); }
        else { if (!*mask) continue; *mask &= ~bit; blk(w, cd); }
    }
}
static void cool_off_all(W *w, int *mask) {             /* pretract l.770: each coolant still on, its off code */
    for (int k = 0; k < 10; k++) if (*mask & (1 << k)) { const char *cd = cool_code(k, 0); if (cd) blk(w, cd); }
    *mask = 0;
}
static void mod_clear(W *w) { w->G[0] = w->X[0] = w->Y[0] = w->Z[0] = w->F[0] = 0; w->have_S = 0; w->cycle = 0; }
/* modal words: G (unless forced), changed X/Y/Z, extra, changed F (not on rapids) */
static const char *words_pre;   /* G40 / G41 D.. / G42 D.. after the motion word (Generic Fanuc 3X Mill.pst: sgcode, sgabsinc, pccdia) */
static const char *words_plane; /* batch 10: G18 / G19 / G17 before the motion word (the pst's sgplane, fstrsel sg17 plane$) */
static void words(W *w, int g, const double *xyz, const char *extra, const double *feed, char *line) {
    char t[24], gs[8]; line[0] = 0;
    snprintf(gs, sizeof gs, "%d", g);
    if (words_plane) { sprintf(line + strlen(line), "%s ", words_plane); w->G[0] = 0; }
    if (strcmp(w->G, gs) || words_pre) { sprintf(line + strlen(line), "G%d ", g); strcpy(w->G, gs); }
    if (words_pre) sprintf(line + strlen(line), "%s ", words_pre);
    char *mods[3] = { w->X, w->Y, w->Z };
    for (int k = 0; k < 3; k++) { fmtn(t, xyz[k], w->dec); if (strcmp(mods[k], t)) { sprintf(line + strlen(line), "%c%s ", "XYZ"[k], t); strcpy(mods[k], t); } }
    if (extra && extra[0]) sprintf(line + strlen(line), "%s ", extra);
    if (feed && g != 0) { fmtn(t, *feed, w->fdec); if (strcmp(w->F, t)) { sprintf(line + strlen(line), "F%s ", t); strcpy(w->F, t); } }
    size_t L = strlen(line); if (L && line[L - 1] == ' ') line[L - 1] = 0;
}

/* the op's planes on this 3-axis post: the Tplane must be the WCS (else rotary / multi-axis output, not supported); the
   work offset: Manual n -> the Generic Fanuc 3X Mill.pst pwcs rule (workofs$ < 6: G(54 + n), else G54.1 P(n - 5));
   Automatic: G54 when the WCS is Top (every program so far), else NOT DECODED (BuildWorkOffsetMap) - refused */
static int op_tplane_wcs(const CamParams *p) {   /* the same plane: matrix (system id) and origin */
    int t = p->tplane <= 0 ? 1 : p->tplane, w = p->wcs <= 0 ? 1 : p->wcs;
    return t == w && !memcmp(p->torg, p->worg, sizeof p->torg);
}
static int op_postable(const CamOp *op, char *err, size_t errn, int i) {
    const CamParams *p = &op->p;
    if (!op_tplane_wcs(p)) { snprintf(err, errn, "operation %d: its tool plane (%d) is not its WCS (%d) - this 3-axis post has no rotary / multi-axis output", i + 1, p->tplane <= 0 ? 1 : p->tplane, p->wcs <= 0 ? 1 : p->wcs); return 0; }
    if (!p->wofs_manual && p->wcs > 1) { snprintf(err, errn, "operation %d: an Automatic work offset for a WCS other than Top is not decoded - set a Manual work offset on its Planes page", i + 1); return 0; }
    return 1;
}
static void wcs_word(const CamParams *p, char *o, size_t n) {
    if (!p || !p->wofs_manual || p->wofs <= 0) snprintf(o, n, "G54");
    else if (p->wofs < 6) snprintf(o, n, "G%d", 54 + p->wofs);
    else snprintf(o, n, "G54.1 P%d", p->wofs - 5);
}
int nc_post_part(FILE *out, McamFile *f, CamOp **camops, int ncamops, const NcPostOpts *o, char *err, size_t errn) {
    NcPostOpts def = { "r180", NULL, NULL };
    if (!o) o = &def;
    int quad = o->style && !strcmp(o->style, "ij-quad"), ij = quad;
    if (f->metric == 2) { snprintf(err, errn, "the part mixes inch and metric operations"); return 0; }
    if (f->metric < 0 && f->path[0]) {                   /* units come from its stored toolpath; G20 / G21 must not be guessed */
        snprintf(err, errn, "the part's units are not known (it has no Mastercam toolpath to read them from)"); return 0; }
    if (f->path[0] && f->nci_off) {                      /* the stored toolpaths are read again at the offsets found on load */
        struct stat st;
        if (stat(f->path, &st) != 0 || (long long)st.st_size != f->file_size || (long long)st.st_mtime != f->file_mtime) {
            snprintf(err, errn, "the part file changed on disk since it was opened - open it again before posting"); return 0; }
    }
    int inch = !mcam_is_metric(f);                       /* the program's units: IsMetric() (formats/units.md) */
    const double tiny = inch ? 0.01 / 25.4 : 0.01;   /* arcs with a chord below 0.01 mm post as G1 (measured, formats/post.md); the same length in inch */
    /* the stored NCI */
    uint8_t *d = NULL; size_t len = 0;
    if (f->path[0] && f->nci_off) {
        FILE *in = fopen(f->path, "rb");
        if (in) { fseek(in, 0, SEEK_END); len = (size_t)ftell(in); fseek(in, 0, SEEK_SET); d = xmalloc(len ? len : 1); if (fread(d, 1, len, in) != len) len = 0; fclose(in); }
    }
    /* events in Operations Manager order (F.ops order): ours from their moves, Mastercam's from the stored NCI */
    EvList ev = { 0 }; static char tnames[256][80]; int tnums[256], ntn = 0; char material[128] = "";
    int nposted = 0, nmissing = 0, d_failed = f->path[0] && f->nci_off && !d; char missing[200] = "";
    for (int i = 0; i < f->nops; i++) {
        if (o->only && !o->only[i]) continue;
        const CamOp *mine = NULL;
        for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) mine = camops[j];
        /* a lathe op (stored or regenerated here, CAM_LFACE): this is a 3-axis mill post - lathe posting is not decoded (decision
           P4: lathe posts skipped); its X / Z moves would come out as mill G-code. Refused, never posted. The lathe opcodes:
           mccore op_is_lathe @1800c6060 (nci_write.c) */
        if ((mine && mine->p.type == CAM_LFACE) || (!mine && ((uint16_t)(f->ops[i].opcode - 0x3c) <= 12 || (uint16_t)(f->ops[i].opcode - 0xc9) <= 0x23))) {
            free(ev.e); free(d); snprintf(err, errn, "operation %d is a lathe operation (%s): lathe posts are not supported here (decision P4) - leave it out of the post",
                                         i + 1, mine ? cam_type_label(mine->p.type) : f->ops[i].type[0] ? f->ops[i].type : "lathe"); return 0; }
        int before = ev.n;
        if (!mine && f->ops[i].stale) {                    /* its tool was changed after Mastercam computed it */
            free(ev.e); free(d); snprintf(err, errn, "operation %d's toolpath was made for a tool that has since been changed - regenerate it first", i + 1); return 0; }
        if (mine && !op_postable(mine, err, errn, i)) { free(ev.e); free(d); return 0; }
        if (mine && mine->gen_units && mine->gen_units - 1 != !inch) {   /* computed before a units change (CamOp.gen_units). Mastercam keeps such
               an op as it is - not dirty, parameters not converted (formats/units.md 9); its NCI keeps the units it was made in (1016 +0x44) */
            free(ev.e); free(d); snprintf(err, errn, "operation %d's toolpath was computed in %s and the part is now in %s - regenerate it first%s", i + 1,
                                          mine->gen_units == 2 ? "mm" : "inch", inch ? "inch" : "mm",
                                          mine->dirty ? "" : " (not dirty: Regenerate all selected)"); return 0; }
        if (!mine && (f->ops[i].nci || d) && (f->nci_metric == 0 || f->nci_metric == 1) && f->nci_metric != !inch) {
            /* a stored toolpath keeps the units Mastercam made it in (1016 met_tool); after a units change (cui_units_set) it is not
               converted (no conversion of operations is decoded, formats/units.md) - refused, never posted at the wrong scale */
            free(ev.e); free(d); snprintf(err, errn, "operation %d's toolpath is in %s and the part is now in %s - regenerate it first", i + 1,
                                          f->nci_metric ? "mm" : "inch", inch ? "inch" : "mm"); return 0; }
        if (mine) camop_events(f, mine, f->ops[i].id, &ev, tnames, tnums, &ntn, 1);
        else if (f->ops[i].nci)                          /* rewritten here (Reverse toolpath, nci_reverse.c) */
            stored_events(f->ops[i].nci, f->ops[i].nci_n, 0, f->ops[i].id, &ev, tnames, tnums, &ntn, material, sizeof material);
        else if (d) stored_events(d, len, f->nci_off, f->ops[i].id, &ev, tnames, tnums, &ntn, material, sizeof material);
        if (ev.n > before) nposted++;
        else { size_t l = strlen(missing); if (l < sizeof missing - 8) snprintf(missing + l, sizeof missing - l, "%s%d", nmissing ? ", " : "", i + 1); nmissing++; }
    }
    free(d);
    if (!nposted) { free(ev.e); snprintf(err, errn, "no toolpaths to post"); return 0; }
    if (nmissing) {                                      /* never a program with operations silently left out */
        free(ev.e); snprintf(err, errn, "operation(s) %s have no toolpath to post%s - regenerate them or leave them out",
                             missing, d_failed ? " (the part file could not be read again)" : ""); return 0;
    }
    for (int i = 0; i < ev.n; i++) {                     /* nothing absurd reaches the program */
        Ev *e = &ev.e[i]; const char *bad = NULL;
        for (int k = 0; k < 3 && !bad; k++) if (!isfinite(e->p[k]) || fabs(e->p[k]) > 1e6 || !isfinite(e->c[k]) || fabs(e->c[k]) > 1e6) bad = "an invalid coordinate";
        if (!bad && (!isfinite(e->feed) || fabs(e->feed) > 1e6 || !isfinite(e->rplane) || fabs(e->rplane) > 1e6 || !isfinite(e->peck))) bad = "an invalid feed / R plane";
        if (!bad && e->kind == EV_TOOL && e->rpm == 0) bad = "spindle speed 0";
        if (!bad && (e->kind == EV_FEED || e->kind == EV_CW || e->kind == EV_CCW || e->kind == EV_DRILL) && !(e->feed > 0)) bad = "a cutting move without a feed rate";
        if (bad) { snprintf(err, errn, "operation %d has %s", e->op, bad); free(ev.e); return 0; }
        /* decisions batch dec2 (Q9 / Q7): XZ / YZ arcs are posted (G18 / G19) only with MCAM_ARC_FILTER_PLANES=1; a stored
           toolpath with them (corpus 3D ops: NCI arc plane 1 / 2) is refused without it (before: written as G17 arcs - wrong) */
        if ((e->kind == EV_CW || e->kind == EV_CCW) && e->plane && !cam_arc_filter_planes_on()) {
            snprintf(err, errn, "operation %d: the toolpath has %s arcs (G%d) - posted only with MCAM_ARC_FILTER_PLANES=1 (formats/post.md \"Arc filter\")",
                     e->op, e->plane == 2 ? "XZ" : "YZ", e->plane == 2 ? 18 : 19); free(ev.e); return 0; }
    }
    /* cutter compensation in control (batch 5, formats/post.md): only with MCAM_CUTTER_COMP=1; never from a stored NCI (its cc
       codes and 1004 records are not ported: before batch 5 such an op was posted without G41 / G42, on the chain - ours: refused);
       LinuxCNC's rules (src/emc/rs274ngc/interp_convert.cc 2.9): the move with G41 / G42 longer than the tool radius in XY
       (convert_straight_comp1 "Length of cutter compensation entry move is not greater than the tool radius"), the move after
       G40 straight ("must be straight, not an arc": the G40 move here is always a line) */
    { int on = nc_cutter_comp_on(); double prv[3] = { 0, 0, 0 };
      for (int i = 0; i < ev.n; i++) {
        Ev *e = &ev.e[i];
        if (e->cc && e->ccst) { snprintf(err, errn, "operation %d: Mastercam's toolpath uses cutter compensation in control (G41 / G42) - posting it from the stored toolpath is not supported here; regenerate it in this program", e->op); free(ev.e); return 0; }
        if (e->cc && !on) { snprintf(err, errn, "operation %d uses cutter compensation in control / wear (G41 / G42): set MCAM_CUTTER_COMP=1 to post it (formats/post.md), or set its compensation to Computer", e->op); free(ev.e); return 0; }
        if (e->cc == 41 || e->cc == 42) {   /* decisions batch dec2 (Q29): Control only on LinuxCNC - Wear / Reverse wear refused */
            int ct = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index >= 0 && camops[j]->op_index < f->nops && f->ops[camops[j]->op_index].id == e->op) ct = op_cc_type(&camops[j]->p);
            if (ct == 2 || ct == 3) { snprintf(err, errn, "operation %d uses %s compensation: its path is already offset and LinuxCNC would add the tool table radius again - this post supports Control only (formats/post.md)", e->op, ct == 2 ? "Wear" : "Reverse wear"); free(ev.e); return 0; }
        }
        if (e->cc && (e->kind == EV_CW || e->kind == EV_CCW)) {   /* batch 7 (ours): Slot Mill's record 2 / last record can be its entry /
                                                    exit arc (FUN_180218410); LinuxCNC turns compensation on / off on straight moves only */
            snprintf(err, errn, "operation %d: cutter compensation (G%d) would start or end on an arc - LinuxCNC refuses it; give the pass an entry / exit line (Slot Mill: Perpendicular entry)", e->op, e->cc == 140 ? 40 : e->cc); free(ev.e); return 0; }
        if (e->cc == 41 || e->cc == 42) {
            double R = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index >= 0 && camops[j]->op_index < f->nops && f->ops[camops[j]->op_index].id == e->op) R = camops[j]->p.tool_dia / 2;
            if (hypot(e->p[0] - prv[0], e->p[1] - prv[1]) <= R) { snprintf(err, errn, "operation %d: the move that turns cutter compensation on (G%d) is not longer than the tool radius - LinuxCNC refuses it; lengthen the entry line (Lead In/Out)", e->op, e->cc); free(ev.e); return 0; }
        }
        if (e->kind == EV_TOOL || e->kind == EV_RAPID || e->kind == EV_FEED || e->kind == EV_CW || e->kind == EV_CCW) memcpy(prv, e->p, sizeof prv);
      } }
    int dcyc = nc_drill_cycles_on();                     /* batch 8: MCAM_DRILL_CYCLES=1 (formats/post.md "Drill cycles") */
    for (int i = 0; i < ev.n; i++) if (ev.e[i].kind == EV_DRILL && (ev.e[i].cyc > (dcyc ? 4 : 2) || ev.e[i].cyc < 0)) {   /* default: only G81 / G83 / G73 */
        static const char *cn[] = { "", "", "", "Tap", "Bore #1", "Bore #2", "Misc. #1", "Misc. #2" };
        int c = ev.e[i].cyc;
        if (dcyc && c == 5)   /* pbore2$ writes G86 without P; LinuxCNC 2.9 interp_cycles.cc: NCE_DWELL_TIME_P_WORD_MISSING_WITH_G86 */
            snprintf(err, errn, "operation %d uses the Bore #2 cycle: the post writes G86 without a P word, which LinuxCNC refuses", ev.e[i].op);
        else if (dcyc && c == 6)   /* pmisc1$ = sgm1 "G76" (fine bore); LinuxCNC's G76 is the lathe threading cycle */
            snprintf(err, errn, "operation %d uses the Fine Bore (shift) cycle: the post writes G76, which is a threading cycle in LinuxCNC", ev.e[i].op);
        else if (dcyc && c == 7)   /* pmisc2$: M29 S.. rigid tapping (no M29 in LinuxCNC) */
            snprintf(err, errn, "operation %d uses the Rigid Tapping cycle: the post writes M29, which LinuxCNC does not have", ev.e[i].op);
        else snprintf(err, errn, "operation %d uses the %s drill cycle, which this post does not output%s", ev.e[i].op,
                 c >= 0 && c < 8 ? cn[c] : "custom", !dcyc && c >= 3 && c <= 4 ? " (MCAM_DRILL_CYCLES=1 posts it, formats/post.md)" : "");
        free(ev.e); return 0;
    }
    /* decisions batch dec1 (formats/user_decisions.md B8-2): a drill op with a dwell (NCI 81 +0x38 / record 0x1250) is refused
       without MCAM_DRILL_CYCLES=1 - before, the default post wrote G81 (or G83 / G73) and dropped the dwell silently */
    if (!dcyc) for (int i = 0; i < ev.n; i++) if (ev.e[i].kind == EV_DRILL && ev.e[i].dwell != 0) {
        snprintf(err, errn, "operation %d has a dwell of %g s: the default post would write the cycle without it; set "
                 "MCAM_DRILL_CYCLES=1 to post it (G82 / G89 P, formats/post.md \"Drill cycles\") or set the dwell to 0", ev.e[i].op, ev.e[i].dwell);
        free(ev.e); return 0;
    }
    /* batch 11: MCAM_COOLANT=1 - every op's coolant must have a LinuxCNC code (op_coolant above) */
    int ncool = nc_coolant_on();
    if (ncool) for (int i = 0; i < ev.n; i++) if (ev.e[i].kind == EV_TOOL && (i == 0 || ev.e[i].op != ev.e[i - 1].op)) {
        OpCool oc; op_coolant(f, camops, ncamops, ev.e[i].op, &oc); int bad = 0;   /* V9 thru: sm08_2 = "M8" (l.399) */
        for (int q = 0; q < oc.n; q++) if (!cool_code(oc.e[q].k, oc.e[q].on)) bad = 1;
        if (bad) { snprintf(err, errn, "operation %d turns on %s coolant: LinuxCNC has no code for it (M7 mist / M8 flood / M9 off only) - set it to "
                            "Ignore / Off on the Coolant page", ev.e[i].op, "an unknown"); free(ev.e); return 0; }
    }
    if (dcyc) for (int i = 0; i < ev.n; i++) if (ev.e[i].kind == EV_DRILL && ev.e[i].cyc == 3) {   /* Tap: the tool's pitch */
        const Tool *tl = NULL; int tn = -1;
        for (int j = i; j >= 0 && tn < 0; j--) if (ev.e[j].kind == EV_TOOL) tn = ev.e[j].tool;
        tl = mcam_tool(f, tn);
        if (!tl || !(tl->pitch > 0)) { snprintf(err, errn, "operation %d: a Tap cycle needs the tool's thread pitch (tool %d has none)", ev.e[i].op, tn); free(ev.e); return 0; }
    }

    W w; memset(&w, 0, sizeof w); w.f = out; w.n = 100; w.dec = inch ? 4 : 3; w.fdec = inch ? 2 : 0;
    char name[256]; const char *b = path_sep_r(f->path) ? path_sep_r(f->path) + 1 : f->path;
    snprintf(name, sizeof name, "%s", o->progname ? o->progname : (b[0] ? b : "PROGRAM")); { char *dot = strrchr(name, '.'); if (dot && !o->progname) *dot = 0; }
    for (char *q = name; *q; q++) *q = (char)toupper((unsigned char)*q);
    time_t now = time(NULL); struct tm tm = *localtime(&now); char ds[64]; strftime(ds, sizeof ds, "(DATE=DD-MM-YY - %d-%m-%y TIME=HH:MM - %H:%M)", &tm);
    fprintf(out, "(%s)\n%s\n", name, ds);
    { extern char cad_doc_name[] __attribute__((weak)); char up[1100]; snprintf(up, sizeof up, "%s", f->path[0] || !cad_doc_name ? f->path : cad_doc_name); for (char *q = up; *q; q++) *q = (char)toupper((unsigned char)*q); fprintf(out, "(MCX FILE - %s)\n", up); }
    if (material[0]) { for (char *q = material; *q; q++) *q = (char)toupper((unsigned char)*q); fprintf(out, "(MATERIAL - %s)\n", material); }
    for (int k = 0; k < ntn; k++) { char up[80]; snprintf(up, sizeof up, "%s", tnames[k]); for (char *q = up; *q; q++) *q = (char)toupper((unsigned char)*q); fprintf(out, "( T%d | %s | H%d )\n", tnums[k], up, tnums[k]); }
    blk(&w, inch ? "G20" : "G21"); blk(&w, "G0 G17 G40 G49 G80 G90");

    int started = 0, tool = -1, last_op = -1; double prev[3] = { 0, 0, 0 };
    int fortc = nc_force_tc_on();                         /* batch 12: MCAM_FORCE_TOOL_CHANGE=1 (op_force_tc) */
    int cmask = 0, v9on = 0;                              /* batch 11 (MCAM_COOLANT=1): X-style coolants on (bit k), V9 coolant on (1 flood, 2 mist) */
    int wpl = 0; char plw[8];                             /* batch 10: the plane of the last G18 / G19 arc (0 = G17) */
    char line[256], t1[24], t2[24], t3[24];
    for (int i = 0; i < ev.n; i++) {
        Ev *e = &ev.e[i];
        if (e->kind == EV_TOOL && e->op == last_op) { memcpy(prev, e->p, sizeof prev); continue; }   /* depth-pass tool records */
        if (e->kind == EV_TOOL) {
            const char *comment = "";
            for (int k = 0; k < f->nops; k++) if (f->ops[k].id == e->op) comment = f->ops[k].comment;
            for (int j = 0; j < ncamops; j++) if (camops[j]->op_index >= 0 && camops[j]->op_index < f->nops && f->ops[camops[j]->op_index].id == e->op) comment = camops[j]->name;
            char cup[128]; snprintf(cup, sizeof cup, "%s", comment); for (char *q = cup; *q; q++) *q = (char)toupper((unsigned char)*q);
            last_op = e->op;
            OpCool oc; memset(&oc, 0, sizeof oc); if (ncool) op_coolant(f, camops, ncamops, e->op, &oc);
            int ftc = fortc && started && e->tool == tool && op_force_tc(f, camops, ncamops, e->op);   /* batch 12: 1002 for the same tool */
            if (started && (e->tool != tool || ftc)) {
                cool_off_all(&w, &cmask);                     /* batch 11: pretract - the off code of each coolant on, before "M5" */
                blk(&w, "M5"); blk(&w, v9on ? "G91 G28 Z0. M9" : "G91 G28 Z0."); v9on = 0; blk(&w, "M01"); mod_clear(&w); }
            if (started && e->tool == tool && !ftc) {
                if (cup[0]) fprintf(out, "( %s )\n", cup);
                cool_pos(&w, &oc, 0, &cmask, NULL, 0);           /* batch 11: ptlchg0$ - pcan */
                if (quad || !w.have_S || e->rpm != w.S) { snprintf(line, sizeof line, "S%d %s", abs(e->rpm), e->rpm < 0 ? "M4" : "M3");   /* < 0 = CCW: sm04 "M4" (fstrsel sm04 spdir2) */ blk(&w, line); }
                w.S = e->rpm; w.have_S = 1;
                /* to the op's start (its clearance height) first, as Mastercam's post does ("X.. Y.. Z25." then "Z10."):
                   without it the next op's first rapid ran straight down to the feed plane on a diagonal */
                {   char cw[64] = ""; cool_pos(&w, &oc, 1, &cmask, cw, sizeof cw);   /* "pbld, n$, pscool, e$" (with) */
                    if (oc.v9 && oc.v9 != v9on) { snprintf(cw + strlen(cw), sizeof cw - strlen(cw), " %s", oc.v9 == 2 ? "M7" : "M8"); v9on = oc.v9; }
                    else if (ncool && !oc.v9 && v9on && pv_cool_frompost(pv_machine(f))) { snprintf(cw + strlen(cw), sizeof cw - strlen(cw), " M9"); v9on = 0; }   /* scoolant = sm09 */
                    if (cw[0]) blk(&w, cw + 1); }
                words(&w, EV_RAPID, e->p, NULL, NULL, line); if (line[0]) blk(&w, line);
                cool_pos(&w, &oc, 2, &cmask, NULL, 0);           /* pcom_movea -> pcan2 */
            } else {
                if (cup[0]) fprintf(out, "( %s )\n", cup);
                cool_pos(&w, &oc, 0, &cmask, NULL, 0);           /* batch 11: ptlchg$ - pcan ahead of "T M6" */
                snprintf(line, sizeof line, "T%d M6", e->tool); blk(&w, line);
                fmtn(t1, e->p[0], w.dec); fmtn(t2, e->p[1], w.dec); fmtn(t3, e->p[2], w.dec);
                { const CamParams *cp = NULL; char wc[24];   /* the work offset of the op starting here (ours: Top / Automatic = G54) */
                  for (int j = 0; j < ncamops; j++) if (camops[j]->op_index >= 0 && camops[j]->op_index < f->nops && f->ops[camops[j]->op_index].id == e->op) cp = &camops[j]->p;
                  wcs_word(cp, wc, sizeof wc);
                  snprintf(line, sizeof line, "G0 G90 %s X%s Y%s ", wc, t1, t2); blk(&w, line); }
                fprintf(out, "%s S%d%s\n", e->rpm < 0 ? "M4" : "M3", abs(e->rpm), tool < 0 ? " " : "");
                w.S = e->rpm; w.have_S = 1;
                /* tool length: H0 as the user's post (LinuxCNC touch-off); MCAM_H_OFFSET=tool gives H<tool> */
                const char *hm = getenv("MCAM_H_OFFSET");
                int h = 0; if (hm && !strcmp(hm, "tool")) { const Tool *tl = mcam_tool(f, e->tool); h = tl && tl->len_off > 0 ? tl->len_off : e->tool; }
                snprintf(line, sizeof line, "G43 H%d Z%s", h, t3);
                {   char cw[64] = ""; cool_pos(&w, &oc, 1, &cmask, cw, sizeof cw);   /* batch 11: pscool on the G43 line (with) */
                    if (oc.v9) { snprintf(cw + strlen(cw), sizeof cw - strlen(cw), " %s", oc.v9 == 2 ? "M7" : "M8"); v9on = oc.v9; }
                    snprintf(line + strlen(line), sizeof line - strlen(line), "%s", cw); }
                blk(&w, line);
                cool_pos(&w, &oc, 2, &cmask, NULL, 0);           /* pcom_movea -> pcan2 */
                strcpy(w.G, "0"); strcpy(w.X, t1); strcpy(w.Y, t2); strcpy(w.Z, t3);
                tool = e->tool; started = 1;
            }
            memcpy(prev, e->p, sizeof prev);
        } else if (!started) continue;
        else if (e->kind == EV_DRILL) {
            char xs[24], ys[24], zs[24], rs[24], fs[24], qs[24], wds[80] = "";
            fmtn(xs, e->p[0], w.dec); fmtn(ys, e->p[1], w.dec); fmtn(zs, e->p[2], w.dec); fmtn(rs, e->rplane, w.dec); fmtn(fs, e->feed, w.fdec);
            if (strcmp(w.X, xs)) { strcat(wds, " X"); strcat(wds, xs); }
            if (strcmp(w.Y, ys)) { strcat(wds, " Y"); strcat(wds, ys); }
            if (!w.cycle) {
                char ps[24];
                if (e->peck > 0) { fmtn(qs, e->peck, w.dec); snprintf(line, sizeof line, "G99 %s%s Z%s R%s Q%s F%s", e->cyc == 2 ? "G73" : "G83", wds, zs, rs, qs, fs); }   /* pchpbrk$ = ppeck$ with sg73 (Generic Fanuc 3X Mill.pst) */
                /* batch 8, MCAM_DRILL_CYCLES=1 only (Generic Fanuc 3X Mill.pst pdrlcommonb l.1071: drlgsel = fsg2(dwell$) + drillcyc$ x 2
                   -> sg81 / sg81d G82 / sg85 G85 / sg85d G89, Tap fsg1(-ss$): sg84 G84 / sg84d G74; pdrill$ / pbore1$: ... R, dwell$
                   ("P" fs2 11: 3 decimals), F; ptap$: a G94 block, then ... R, F = pitch x speed (fs2 17: inch 2 / metric 3 decimals)) */
                else if (dcyc && e->cyc == 0 && e->dwell != 0) { fmtn(ps, e->dwell, 3); snprintf(line, sizeof line, "G99 G82%s Z%s R%s P%s F%s", wds, zs, rs, ps, fs); }
                else if (dcyc && e->cyc == 4) {
                    if (e->dwell != 0) { fmtn(ps, e->dwell, 3); snprintf(line, sizeof line, "G99 G89%s Z%s R%s P%s F%s", wds, zs, rs, ps, fs); }
                    else snprintf(line, sizeof line, "G99 G85%s Z%s R%s F%s", wds, zs, rs, fs);
                } else if (dcyc && e->cyc == 3) {
                    const Tool *tl = mcam_tool(f, tool);          /* checked above */
                    double pit = tl ? tl->pitch : 0;               /* ptap$: met_tool$ / toolismetric conversions = the pitch in the program's units */
                    if (tl && tl->is_metric && inch) pit /= 25.4; else if (tl && !tl->is_metric && !inch) pit *= 25.4;
                    char tf[24]; fmtn(tf, pit * abs(w.S), inch ? 2 : 3);
                    blk(&w, "G94");
                    snprintf(line, sizeof line, "G99 %s%s Z%s R%s F%s", w.S < 0 ? "G74" : "G84", wds, zs, rs, tf);
                    strcpy(fs, tf); w.tapping = 1;
                }
                else snprintf(line, sizeof line, "G99 G81%s Z%s R%s F%s", wds, zs, rs, fs);
                blk(&w, line); w.cycle = 81;
            } else if (wds[0]) blk(&w, wds + 1);
            strcpy(w.X, xs); strcpy(w.Y, ys); strcpy(w.F, fs);
            strcpy(w.Z, rs);                                     /* G99: the tool waits at the R plane between holes */
            prev[0] = e->p[0]; prev[1] = e->p[1]; prev[2] = e->rplane;
        } else if (e->kind == EV_DRILL_END) {
            if (w.cycle) { blk(&w, "G80"); w.cycle = 0; w.G[0] = 0; }
            if (w.tapping) { blk(&w, "G94"); w.tapping = 0; w.F[0] = 0; }   /* pcanceldc$: G94 after a tap (tap_feedtype 1); the feed
                                                    format reset (newfs(15, feed)): the next F is written again */
        } else if (e->kind == EV_RAPID || e->kind == EV_FEED) {
            char ccw[24] = "";
            if (e->cc == 41 || e->cc == 42) snprintf(ccw, sizeof ccw, "G%d D%d", e->cc, e->dnum);   /* pccdia: sccomp, tloffno$ */
            else if (e->cc == 140) snprintf(ccw, sizeof ccw, "G40");
            words_pre = ccw[0] ? ccw : NULL;
            if (wpl) { words_plane = "G17"; wpl = 0; }          /* ours: back to XY after a G18 / G19 arc of the filter */
            words(&w, e->kind, e->p, NULL, e->kind == EV_FEED ? &e->feed : NULL, line);
            words_pre = NULL; words_plane = NULL;
            if (line[0]) blk(&w, line);
            memcpy(prev, e->p, sizeof prev);
        } else if (e->kind == EV_CW || e->kind == EV_CCW) {
            int g = e->kind, u = AX[e->plane][0], v = AX[e->plane][1], dp = 3 - u - v;
            /* batch 10: an XZ / YZ arc of the Line/Arc filter (MCAM_ARC_FILTER_PLANES=1): G18 / G19 (the pst's sgplane), the XZ
               direction as the NCI writes it (the reverse of the (x, z) frame: FUN_18094ce10) - LinuxCNC G18 / G19 follow the same rule */
            /* dec2 (Q9): Mastercam's stored XZ / YZ arcs too (read into the same frame; before they were posted without G18 / G19) */
            int pg = e->plane ? (e->plane == 2 ? 18 : 19) : 17, gout = e->plane == 2 ? (g == EV_CW ? EV_CCW : EV_CW) : g;
            if (pg != 17 || wpl) { if (pg != (wpl ? wpl : 17)) { snprintf(plw, sizeof plw, "G%d", pg); words_plane = plw; } wpl = pg == 17 ? 0 : pg; }
            double sx = prev[u], sy = prev[v], rad = hypot(sx - e->c[u], sy - e->c[v]);
            double a0 = atan2(sy - e->c[v], sx - e->c[u]), a1 = atan2(e->p[v] - e->c[v], e->p[u] - e->c[u]);
            double sw = g == 3 ? fmod(a1 - a0 + 4 * M_PI, 2 * M_PI) : fmod(a0 - a1 + 4 * M_PI, 2 * M_PI);
            int helix = fabs(e->p[dp] - prev[dp]) > 1e-9;
            if (sw < 1e-9) sw = 2 * M_PI;                        /* start == end: full circle */
            if (hypot(e->p[u] - sx, e->p[v] - sy) < tiny && sw < M_PI) {   /* tiny arc -> straight line (as posted) */
                words(&w, 1, e->p, NULL, &e->feed, line); if (line[0]) blk(&w, line); words_plane = NULL;
                memcpy(prev, e->p, sizeof prev); continue;
            }
            double stops[16]; int ns = 0;
            if (quad) {                                          /* break at every quadrant boundary */
                double step = M_PI / 2; long k = g == 3 ? (long)floor(a0 / step) + 1 : (long)ceil(a0 / step) - 1;
                for (;;) {
                    double dd = g == 3 ? k * step - a0 : a0 - k * step;
                    if (dd >= sw - 1e-7 || ns >= 14) break;
                    if (dd > 1e-7) stops[ns++] = dd;
                    k += g == 3 ? 1 : -1;
                }
                stops[ns++] = sw;
            } else {                                             /* pieces of at most 180 degrees (Mastercam's arcs) */
                /* refitted arcs: at most 120 degrees - their endpoints sit only within tol of the circle, and an R word
                   near 180 degrees moves the centre by ~sqrt(2 R err) (cam2d.c splits its own output the same way) */
                double lim = e->fitted ? 2 * M_PI / 3 : M_PI;
                int np = (int)ceil(sw / lim - 1e-9); if (np < 1) np = 1;
                for (int k = 1; k <= np && ns < 16; k++) stops[ns++] = e->fitted ? fmin(k * (sw / np), sw) : fmin(k * M_PI, sw);
            }
            double cur[3] = { prev[0], prev[1], prev[2] };
            for (int k = 1; k <= ns; k++) {
                double done = stops[k - 1], frac = done / sw, ang = a0 + (g == 3 ? done : -done), pe[3] = { e->p[0], e->p[1], e->p[2] };
                if (k < ns) { pe[u] = e->c[u] + rad * cos(ang); pe[v] = e->c[v] + rad * sin(ang); pe[dp] = prev[dp] + (e->p[dp] - prev[dp]) * frac; }
                double psw = done - (k > 1 ? stops[k - 2] : 0);
                /* arc words from the already-rounded start and centre (formats/post.md) */
                double cs = rnd(cur[u], w.dec), cv = rnd(cur[v], w.dec), ii = rnd(e->c[u], w.dec) - cs, jj = rnd(e->c[v], w.dec) - cv, rr = hypot(ii, jj);
                if (hypot(rnd(pe[u], w.dec) - cs, rnd(pe[v], w.dec) - cv) < tiny && ns > 1) {   /* tiny leftover piece -> line */
                    words(&w, 1, pe, NULL, &e->feed, line); if (line[0]) blk(&w, line); words_plane = NULL;
                    memcpy(cur, pe, sizeof cur); continue;
                }
                char extra[64], a[24], bb[24];
                if (helix || ij) { fmtn(a, ii, w.dec); fmtn(bb, jj, w.dec); snprintf(extra, sizeof extra, "%c%s %c%s", "IJK"[u], a, "IJK"[v], bb); }
                else { fmtn(a, psw > M_PI + 1e-9 ? -rr : rr, w.dec); snprintf(extra, sizeof extra, "R%s", a); }
                words(&w, gout, pe, extra, &e->feed, line); blk(&w, line); words_plane = NULL;
                memcpy(cur, pe, sizeof cur);
            }
            words_plane = NULL;
            memcpy(prev, e->p, sizeof prev);
        }
    }
    if (started) { cool_off_all(&w, &cmask); blk(&w, "M5"); blk(&w, v9on ? "G91 G28 Z0. M9" : "G91 G28 Z0."); blk(&w, "G28 X0. Y0."); blk(&w, "M30"); fprintf(out, "M2\n"); }
    free(ev.e);
    return nposted;                                       /* > 0 = the count of operations posted */
}

/* ------------------------------------------------------------------------------------------------ backplot motions */
/* The motions the post would output, in program order (Backplot, formats/backplot.md): Mastercam's ops from their
   stored NCI, ours from their generated moves - the same event extraction as nc_post_part. */
int nc_motions(const McamFile *f, CamOp **camops, int ncamops, const char *only, NcMotion **out) {
    *out = NULL;
    uint8_t *d = NULL; size_t len = 0;
    if (f->path[0] && f->nci_off) {
        FILE *in = fopen(f->path, "rb");
        if (in) { fseek(in, 0, SEEK_END); long L = ftell(in); fseek(in, 0, SEEK_SET);
            if (L > 0) { len = (size_t)L; d = xmalloc(len); if (fread(d, 1, len, in) != len) { free(d); d = NULL; len = 0; } } fclose(in); }
    }
    static char tnames[256][80]; int tnums[256], ntn = 0; char material[128];
    NcMotion *m = NULL; int n = 0, cap = 0; double cur[3] = { 0, 0, 0 }; int have = 0;
    for (int i = 0; i < f->nops; i++) {
        if (only && !only[i]) continue;
        const CamOp *mine = NULL;
        for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) mine = camops[j];
        EvList ev = { 0 };
        if (mine) camop_events(f, mine, f->ops[i].id, &ev, tnames, tnums, &ntn, 0);
        else if (f->ops[i].nci)                          /* rewritten here (Reverse toolpath, nci_reverse.c) */
            stored_events(f->ops[i].nci, f->ops[i].nci_n, 0, f->ops[i].id, &ev, tnames, tnums, &ntn, material, sizeof material);
        else if (d) stored_events(d, len, f->nci_off, f->ops[i].id, &ev, tnames, tnums, &ntn, material, sizeof material);
        int tool = -1, rpm = 0;
        for (int k = 0; k < ev.n; k++) {
            const Ev *e = &ev.e[k];
            if (e->kind == EV_DRILL_END) continue;
            if (e->kind == EV_DRILL && e->has_s) {          /* FUN_67dc9540: rapid to S, rapid to R, feed to the hole, back to S */
                double S[3] = { e->s[0], e->s[1], e->s[2] }, B[3] = { e->p[0], e->p[1], e->p[2] }, P[4][3]; int kinds[4], np = 0;
                double dl = hypot(hypot(B[0] - S[0], B[1] - S[1]), B[2] - S[2]);
                memcpy(P[np], S, sizeof S); kinds[np++] = NCM_RAPID;
                if (fabs(e->rdist) > 1e-9 && dl > 0) { for (int k = 0; k < 3; k++) P[np][k] = S[k] + (B[k] - S[k]) / dl * e->rdist; kinds[np++] = NCM_RAPID; }
                memcpy(P[np], B, sizeof B); kinds[np++] = NCM_LINE;
                memcpy(P[np], S, sizeof S); kinds[np++] = e->cyc == 3 ? NCM_LINE : NCM_RAPID;
                for (int s2 = 0; s2 < np; s2++) {
                    if (n == cap) { cap = cap ? cap * 2 : 1024; m = xrealloc(m, (size_t)cap * sizeof *m); }
                    NcMotion *q = &m[n]; memset(q, 0, sizeof *q);
                    q->op_index = i; q->tool = tool; q->rpm = rpm; q->kind = kinds[s2]; q->feed = kinds[s2] == NCM_LINE ? e->feed : -1;
                    memcpy(q->a, have ? cur : P[s2], sizeof q->a); memcpy(q->b, P[s2], sizeof q->b);
                    q->len = hypot(hypot(q->b[0] - q->a[0], q->b[1] - q->a[1]), q->b[2] - q->a[2]);
                    memcpy(cur, q->b, sizeof cur); have = 1; n++;
                }
                continue;
            }
            if (n == cap) { cap = cap ? cap * 2 : 1024; m = xrealloc(m, (size_t)cap * sizeof *m); }
            NcMotion *q = &m[n]; memset(q, 0, sizeof *q);
            q->op_index = i; q->feed = e->feed;
            if (e->kind == EV_TOOL) { tool = e->tool; rpm = e->rpm; q->kind = NCM_TOOL; }
            else if (e->kind == EV_RAPID) q->kind = NCM_RAPID;
            else if (e->kind == EV_FEED) q->kind = NCM_LINE;
            else if (e->kind == EV_CW || e->kind == EV_CCW) { q->kind = e->kind == EV_CW ? NCM_CW : NCM_CCW; q->plane = e->plane; memcpy(q->c, e->c, sizeof q->c); }
            else if (e->kind == EV_DRILL) { q->kind = NCM_DRILL; q->rplane = e->rplane; q->peck = e->peck; q->cyc = e->cyc; }
            else continue;
            q->tool = tool; q->rpm = rpm;
            memcpy(q->a, have ? cur : e->p, sizeof q->a);
            memcpy(q->b, e->p, sizeof q->b);
            if (q->kind == NCM_DRILL) q->a[0] = q->b[0], q->a[1] = q->b[1], q->a[2] = e->rplane;   /* from the R plane down */
            if (q->kind == NCM_TOOL) memcpy(q->a, q->b, sizeof q->a);
            if (q->kind == NCM_CW || q->kind == NCM_CCW) {
                int u = q->plane == 1 ? 1 : 0, v = q->plane == 0 ? 1 : 2, w = 3 - u - v;
                double r = hypot(q->a[u] - q->c[u], q->a[v] - q->c[v]);
                double a0 = atan2(q->a[v] - q->c[v], q->a[u] - q->c[u]), a1 = atan2(q->b[v] - q->c[v], q->b[u] - q->c[u]);
                double sw = q->kind == NCM_CCW ? fmod(a1 - a0 + 4 * M_PI, 2 * M_PI) : fmod(a0 - a1 + 4 * M_PI, 2 * M_PI);
                if (sw < 1e-9) sw = 2 * M_PI;
                q->r = r; q->a0 = a0; q->sweep = q->kind == NCM_CCW ? sw : -sw;
                q->len = hypot(r * sw, q->b[w] - q->a[w]);
            } else q->len = hypot(hypot(q->b[0] - q->a[0], q->b[1] - q->a[1]), q->b[2] - q->a[2]);
            if (q->kind == NCM_DRILL) q->len *= 2;                       /* down and back to R (G99) */
            memcpy(cur, q->kind == NCM_DRILL ? q->a : q->b, sizeof cur); have = 1;
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
        p[u] = m->c[u] + m->r * cos(a); p[v] = m->c[v] + m->r * sin(a); p[w] = m->a[w] + (m->b[w] - m->a[w]) * t;
        return;
    }
    if (m->kind == NCM_DRILL) {                                         /* down to depth, then up to R */
        double s = t < 0.5 ? t * 2 : (1 - t) * 2;
        for (int k = 0; k < 3; k++) p[k] = m->a[k] + (m->b[k] - m->a[k]) * s;
        return;
    }
    for (int k = 0; k < 3; k++) p[k] = m->a[k] + (m->b[k] - m->a[k]) * t;
}

/* ------------------------------------------------------------------------------------------------ NCI extents */
/* Stock Setup "NCI extents" (25077): mastercam.dll FUN_1801ef250 -> get_bnci_extents(nci, NULL, 1, 0, 0, &min, &max,
   &found) @1800fd670 -> FUN_1800fda60 (decomp/mccore_bnci_extents.c; spec: the decode notes in formats/mcam.md "Stock
   Setup"). The ops: GetOpIdsFromGroup(active machine group, 1) = every op of the group and its child groups (record +0x5bc
   = the machine group id). Mastercam's ops from their stored NCI records; ops made here from their generated moves with
   the records Mastercam would write for them: 1013 (bnci_wr_1013 @18008e540: +0x20 = op +0x2e8 = the tool's
   OverallDiameter, +0x28 = op +0x2f0 = its CornerRadius, i16 +0x30 = op_is_tip_comp == 1), the Top plane (no 1014). */
typedef struct { double mn[3], mx[3]; int mach; } XAcc;
static void xa_add(XAcc *a, const double q[3]) {          /* FUN_180100750: per-axis min / max; lathe zeroes max.z, min.y, min.z */
    for (int i = 0; i < 3; i++) { if (q[i] < a->mn[i]) a->mn[i] = q[i]; if (q[i] > a->mx[i]) a->mx[i] = q[i]; }
    if (a->mach == 1) { a->mx[2] = 0; a->mn[1] = 0; a->mn[2] = 0; }
}
typedef struct {                                           /* FUN_1800fda60's running state */
    XAcc a; double O[3], tm[9]; int hm, k30, active, seen, ntc, has_pend, has_prev; double h, d28, pend[3], prev[3], zcap;
    double tol, chord; int undec;
} XSt;
static void xs_v2w(const XSt *s, const double p[3], double w[3]) {   /* view_to_world(p + O, V): view < 2 identity, else M^T p */
    double q[3] = { p[0] + s->O[0], p[1] + s->O[1], p[2] + s->O[2] };
    if (!s->hm) { w[0] = q[0]; w[1] = q[1]; w[2] = q[2]; return; }
    for (int i = 0; i < 3; i++) w[i] = q[0] * s->tm[i] + q[1] * s->tm[3 + i] + q[2] * s->tm[6 + i];
}
static void xs_addz(XSt *s, const double p[3], double dz) { double q[3] = { p[0], p[1], p[2] - dz }, w[3]; xs_v2w(s, q, w); xa_add(&s->a, w); }
static void xs_cap(XSt *s) {                               /* FUN_1800fd280 at -3 / -7 (mill): max.z capped (view: Top assumed) */
    if (s->a.mach == 1) return;
    if (s->zcap != -1e38 && s->zcap < s->a.mx[2]) s->a.mx[2] = s->zcap;
    s->zcap = -1e38;
}
static void xs_tool(XSt *s, int cap_ok, double top, double depth) {   /* 1000-1002 [E:1656-1748] */
    s->has_pend = 0; s->ntc++;
    if (s->a.mach == 3 || !cap_ok) return;
    double p1[3] = { 0, 0, top }, p2[3] = { 0, 0, depth }, w1[3], w2[3];
    xs_v2w(s, p1, w1); xs_v2w(s, p2, w2);
    double z = w1[2] > w2[2] ? w1[2] : w2[2]; if (z > s->zcap) s->zcap = z;
}
static void xs_rapid(XSt *s, const double p[3]) { xs_v2w(s, p, s->pend); s->has_pend = 1; memcpy(s->prev, p, sizeof s->prev); s->has_prev = 1; }
static void xs_feed(XSt *s, const double p[3]) {           /* feed point [E:1437-1648]: P, P - d28 (mill, not tip comp), P - D / 2 */
    if (s->has_pend) xa_add(&s->a, s->pend);
    xs_addz(s, p, 0);
    if (s->a.mach == 0 && s->k30 == 0) xs_addz(s, p, s->d28);
    if (s->a.mach != 3) xs_addz(s, p, s->h);
    memcpy(s->prev, p, sizeof s->prev); s->has_prev = 1;
}
/* arcs 2 / 3 (plane 0) [E:932-1436]: from the previous record's point S (bnci_world_points) - BnciToEntity -> constr_ns_arc,
   then break_arc_n @1804b50f0 into n = round(|sweep| / sqrt(8 chord / r)) (FUN_1800fd4a0, 2..20) chords, every point at
   S's depth; else the fallback: the end point and the 0 / 90 / 180 / 270 degree points the arc passes */
static void xs_arc(XSt *s, int cw, int plane, int full, const double e[3], const double c[3]) {
    if (s->has_pend) xa_add(&s->a, s->pend);
    if (plane < 0) { memcpy(s->prev, e, sizeof s->prev); s->has_prev = 1; return; }
    if (plane != 0 || s->hm || !s->has_prev) { s->undec = 1; memcpy(s->prev, e, sizeof s->prev); s->has_prev = 1; return; }   /* NOT DECODED */
    const double *S = s->prev; double cx = c[0], cy = c[1];
    double rs = hypot(S[0] - cx, S[1] - cy), r3 = sqrt((e[0] - c[0]) * (e[0] - c[0]) + (e[1] - c[1]) * (e[1] - c[1]) + (e[2] - c[2]) * (e[2] - c[2]));
    if (rs >= s->tol && !full) {
        double sa = atan2(S[1] - cy, S[0] - cx), ea = atan2(e[1] - cy, e[0] - cx), sw = cw ? sa - ea : ea - sa;
        while (sw <= 0) sw += 2 * M_PI;                    /* constr_ns_arc NOT DECODED in full: the directed sweep S -> E */
        if (cw) sw = -sw;
        double step = sqrt(s->chord * 8.0 / rs); int ns = 2;
        if (fabs(step) > 1e-6) { ns = (int)floor(fabs(sw / step) + 0.5); if (ns > 20) ns = 20; if (ns <= 1) ns = 2; }
        for (int k = 0; k <= ns; k++) { double t = sa + sw * k / ns, pt[3] = { cx + rs * cos(t), cy + rs * sin(t), S[2] }, w[3]; xs_v2w(s, pt, w); xa_add(&s->a, w); }
    } else if (rs >= s->tol && full) {
        s->undec = 1;                                      /* constr_circle's start angle NOT DECODED */
    } else {                                               /* [E:1040-1155] */
        xs_addz(s, e, 0);
        double sa = atan2(S[1] - cy, S[0] - cx) * 180 / M_PI, ea = atan2(e[1] - cy, e[0] - cx) * 180 / M_PI, sw;
        if (sa < 0) sa += 360; if (ea < 0) ea += 360;
        int fl = full || fabs(sa - ea) < s->tol;
        if (fl) { sa = 0; sw = 360; }
        else {
            if (sa <= ea) sw = cw ? -(360.0 - (ea - sa)) : ea - sa;
            else sw = cw ? -(sa - ea) : (360.0 - sa) + ea;
            if (sw < 0) { sw = fabs(sw); sa -= sw; if (sa < 0) sa += 360; }
        }
        double tot = sw + sa, en = tot > 360 ? tot - 360 : tot; int wrap = en < sa;
        double qd[4][2] = { { r3, 0 }, { 0, r3 }, { -r3, 0 }, { 0, -r3 } };
        int use[4] = { fl || wrap, fl || (sa <= 90 && 90 < en) || (wrap && tot >= 450), fl || (sa <= 180 && 180 < en) || (wrap && tot >= 540),
                       fl || (sa <= 270 && 270 < en) || (wrap && tot >= 630) };
        for (int k = 0; k < 4; k++) if (use[k]) { double pc[3] = { c[0] + qd[k][0], c[1] + qd[k][1], c[2] }; xs_addz(s, pc, 0); }
    }
    memcpy(s->prev, e, sizeof s->prev); s->has_prev = 1;
}
/* one stored op's NCI section (records of op id `id` between its -2 and -3) */
static void xs_stored(XSt *s, const uint8_t *d, size_t len, size_t off, int id, const uint8_t *rec, uint32_t recn) {
    uint8_t b[0x371];
    while (off + 2 <= len) {
        uint16_t sz = (uint16_t)(d[off] | d[off + 1] << 8);
        if (sz == 0 || sz > 0x6e0 || off + 2 + sz > len) break;
        memset(b, 0, sizeof b); memcpy(b, d + off + 2, sz < sizeof b - 1 ? sz : sizeof b - 1); off += 2 + sz;
        int g = rd_h(b, 0);
        if (g == -2) {                                     /* [E:790-812]: active when +0x18 != 0 (meaning NOT DECODED) */
            if (rd_i(b, 0x1c) != id) continue;
            if (b[0x18]) { s->active = 1; s->a.mach = 0; } /* lathe / wire op types: not in this program's parts */
            continue;
        }
        if (rd_i(b, 4) != id) continue;                    /* IdInArray(op id +0x04) [E:683] */
        if (g == -3) { xs_cap(s); s->active = 0; break; }
        if (g == 1040) {                                   /* skip to the matching 1041 [E:705-789] */
            while (off + 2 <= len) { uint16_t z2 = (uint16_t)(d[off] | d[off + 1] << 8); if (!z2 || off + 2 + z2 > len) break;
                int g2 = (int16_t)(d[off + 2] | d[off + 3] << 8); off += 2 + z2; if (g2 == 1041) break; }
            continue;
        }
        if (!s->active) continue;
        s->seen++;
        double p[3] = { rd_d(b, 0x20), rd_d(b, 0x28), rd_d(b, 0x30) };   /* bnci_get_pt @1800865c0 */
        if (g == 11) { p[0] = rd_d(b, 0x18); p[1] = rd_d(b, 0x20); p[2] = rd_d(b, 0x28); }
        if (g == 22 || g == 23) p[2] = rd_d(b, 0x40);
        switch (g) {
        case -7: xs_cap(s); break;
        case 1000: case 1001: case 1002: {                 /* the op record: top of stock +0x2e8 / incremental +0x2f0, depth +0x1a0;
                                                              the opcode test passes for every 2D mill opcode (none of 9 / 10 / 15 / 44
                                                              / 107 / 131 / 132 / multisurface) */
            int ok = rec && recn > 0x2f0 && !rec[0x2f0]; double top = 0, dep = 0;
            if (ok) { memcpy(&top, rec + 0x2e8, 8); memcpy(&dep, rec + 0x1a0, 8); }
            xs_tool(s, ok, top, dep); break; }
        case 1013:                                         /* [E:1749-1846] */
            for (int k = 0; k < 3; k++) { double v = rd_d(b, 0x38 + 8 * k); s->O[k] = fabs(v) > 1e38 ? 0 : v; }
            s->hm = 0; s->k30 = rd_h(b, 0x30); s->h = 0.5 * rd_d(b, 0x20); s->d28 = rd_d(b, 0x28); break;
        case 1014: {                                       /* find_view_m(3x3 at +0x18): Top = identity */
            static const double I[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
            for (int k = 0; k < 9; k++) s->tm[k] = rd_d(b, 0x18 + 8 * k);
            s->hm = 0; for (int k = 0; k < 9; k++) if (fabs(s->tm[k] - I[k]) > 1e-12) s->hm = 1;
            break; }
        case 201: { double p1[3] = { rd_d(b, 0x28), rd_d(b, 0x18), 0 }, p2[3] = { rd_d(b, 0x30), rd_d(b, 0x20), 0 }; xa_add(&s->a, p1); xa_add(&s->a, p2); break; }
        case 0: case 20: case 30: xs_rapid(s, p); break;
        case 1: case 11: case 21: case 22: case 23: case 31: case 81: case 100: xs_feed(s, p); break;
        case 2: case 3: { double c[3] = { rd_d(b, 0x38), rd_d(b, 0x40), rd_d(b, 0x48) }; xs_arc(s, g == 2, rd_h(b, 0x18), b[0x5a], p, c); break; }
        case 32: case 33: s->undec = 1; break;             /* 4-axis arcs NOT DECODED */
        default: break;
        }
    }
}
int nc_extents(const McamFile *f, CamOp **camops, int ncamops, const int *top_inc, double tol, double chord, double box[6], int *undecoded) {
    XSt s; memset(&s, 0, sizeof s);
    for (int k = 0; k < 3; k++) { s.a.mn[k] = 1e38; s.a.mx[k] = -1e38; }   /* FUN_1801006d0 (p4 = 1) */
    s.k30 = 1; s.zcap = -1e38; s.tol = tol; s.chord = chord;
    uint8_t *d = NULL; size_t len = 0;
    if (f->path[0] && f->nci_off) {
        FILE *in = fopen(f->path, "rb");
        if (in) { fseek(in, 0, SEEK_END); long L = ftell(in); fseek(in, 0, SEEK_SET);
            if (L > 0) { len = (size_t)L; d = xmalloc(len); if (fread(d, 1, len, in) != len) { free(d); d = NULL; len = 0; } } fclose(in); }
    }
    static char tnames[256][80]; int tnums[256], ntn = 0;
    for (int i = 0; i < f->nops; i++) {
        const Op *o = &f->ops[i];
        if (o->raw && o->raw_n > 0x5c0) { int32_t g; memcpy(&g, o->raw + 0x5bc, 4); if (f->stock_rec && g != f->stock_grp) continue; }
        int j = -1; for (int k = 0; k < ncamops; k++) if (camops[k]->op_index == i) j = k;
        if (j >= 0) {                                      /* made / edited here: its moves as the NCI it would have */
            const CamOp *op = camops[j]; const CamParams *p = &op->p;
            EvList ev = { 0 }; camop_events(f, op, o->id, &ev, tnames, tnums, &ntn, 0);
            if (!ev.n) { free(ev.e); continue; }
            s.active = 1; s.a.mach = 0;                    /* -2 */
            s.O[0] = s.O[1] = s.O[2] = 0; s.hm = 0;        /* 1013 (bnci_wr_1013) */
            s.h = 0.5 * p->tool_dia; s.d28 = p->corner_r;
            s.k30 = p->type == CAM_DRILL && p->tip_comp == 1;   /* op_is_tip_comp @1800c7380: drill op +0x1200 (= tip_comp); the
                                                                  other opcodes' bytes are not in CamParams - k30 only gates the
                                                                  z - corner radius point, never lower than z - D / 2 */
            for (int k = 0; k < ev.n; k++) {
                const Ev *e = &ev.e[k]; s.seen++;
                if (e->kind == EV_TOOL) xs_tool(&s, !(top_inc && top_inc[j]), p->top_z, p->final_z);
                else if (e->kind == EV_RAPID) xs_rapid(&s, e->p);
                else if (e->kind == EV_FEED || e->kind == EV_DRILL) xs_feed(&s, e->p);
                else if (e->kind == EV_CW || e->kind == EV_CCW) xs_arc(&s, e->kind == EV_CW, e->plane, 0, e->p, e->c);
            }
            xs_cap(&s); s.active = 0;                      /* -3 */
            free(ev.e);
        } else if (o->nci) xs_stored(&s, o->nci, o->nci_n, 0, o->id, o->raw, o->raw_n);
        else if (d) xs_stored(&s, d, len, f->nci_off, o->id, o->raw, o->raw_n);
    }
    free(d);
    XAcc *a = &s.a;                                        /* [E:2080-2112] */
    if (fabs(a->mn[0] - a->mx[0]) <= tol) { a->mn[0] -= 2 * tol; a->mx[0] += 2 * tol; }
    if (fabs(a->mn[1] - a->mx[1]) <= 2 * tol) { a->mn[1] -= 2 * tol; a->mx[1] += 2 * tol; }
    if (a->mach == 0 && fabs(a->mn[2] - a->mx[2]) <= 2 * tol) { a->mn[2] -= 2 * tol; a->mx[2] += 2 * tol; }
    for (int k = 0; k < 3; k++) { box[k] = a->mn[k]; box[3 + k] = a->mx[k]; }
    if (undecoded) *undecoded = s.undec;
    return s.ntc > 0 ? s.seen : 0;
}
