/* toollib - tool libraries (see toollib.h). The .tooldb reader follows the table layout in ../formats/tooldb_schema.sql;
 * TlTool.OpToolInfo is the same 0x288-byte record as a part file's TL_ID (offsets in ../formats/tooldb.md). */
#include <dirent.h>
#include <math.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paths.h"
#include "toollib.h"
#include "toolprofile.h"

/* MCToolType names: mccore Cnc::Tool::tl_string_chooser (0x1800dbef0, mill products) -> mccoreres string ids
   5014, 5019, 5020, 5022, 5021, 5023, 5024, 5379-5384, 5025, 5018, 5385, 5026, 5027, 5386, 5387, 5229, 5248, 5255, 5395,
   5412, 5396 (exports/mccoreres_strings.tsv, formats/tooltypes_mill.tsv; decomp/mccore_toolfilt.c) */
static const char *type_names[] = {
    "Undefined", "Center Drill", "Spot Drill", "Drill", "Tap RH", "Tap LH", "Reamer", "Bore", "CBore",
    "CSink", "Endmill1 Flat", "Endmill2 Sphere", "Chamfer mill", "Face mill", "Slot Mill", "Radius mill",
    "Dove mill", "Taper mill", "Lollipop mill", "Endmill3 Bull", "Block Drill", "Engrave tool", "Bradpt Drill",
    "High feed mill", "Thread mill", "Barrel mill" };

const char *tool_type_name(int type) { return type >= 0 && type < (int)(sizeof type_names / sizeof *type_names) ? type_names[type] : "Unknown"; }
int tool_type_count(void) { return (int)(sizeof type_names / sizeof *type_names); }
/* mccore Cnc::Tool::is_mill_tool 0x1800daa80: types < 26 with bit set in 0x3affc00 (10-19, 21, 23, 24, 25);
   is_drill_tool 0x1800daa60 = !is_mill_tool (decomp/mccore_toolfilt.c) */
int tool_is_mill(int type) { return type >= 0 && type < 26 && ((0x3affc00u >> type) & 1); }
int tool_is_drill(int type) { return !tool_is_mill(type); }

void tool_copy(Tool *dst, const Tool *src) {
    *dst = *src;
    if (src->holder_n && src->holder_rz) {
        dst->holder_rz = xmalloc(sizeof(double) * 2 * src->holder_n);
        memcpy(dst->holder_rz, src->holder_rz, sizeof(double) * 2 * src->holder_n);
    } else { dst->holder_rz = NULL; dst->holder_n = 0; }
}

void tool_set_holder(Tool *t, const Holder *h) {
    free(t->holder_rz); t->holder_rz = NULL; t->holder_n = 0; t->holder[0] = 0;
    if (!h) return;
    snprintf(t->holder, sizeof t->holder, "%s", h->name);
    if (h->n) { t->holder_rz = xmalloc(sizeof(double) * 2 * h->n); memcpy(t->holder_rz, h->rz, sizeof(double) * 2 * h->n); t->holder_n = h->n; }
}

/* a new tool = Mastercam's default tool of that type and units: TlAssemblyItemFactory (mccore FUN_180abb8f0) loads
   common\ToolData\DatabaseDefaults.tooldb (FUN_180abc220 / FUN_180abc290) and picks the item whose MCToolType and
   IsMetric match (lambda FUN_180abba80). A caller's diameter replaces only the diameter (TlToolMill::
   SetOverallDiameter stores it alone); a sphere mill keeps a full radius (both its defaults: r = dia / 2). Returns 0 (tool zeroed but for type /
   number / diameter) if the file or the type is missing. */
static int load_tooldb(const char *path, ToolLib *L, char *err, int errn);
int tool_new(Tool *t, int type, double dia, int num, int metric) {
    static ToolLib D; static int tried;
    if (!tried) { char err[256] = "no shared folder set (paths.conf: shared) - no default tools"; tried = 1; char p[1100]; site_file(p, sizeof p, "shared", "common/ToolData/DatabaseDefaults.tooldb"); if (!p[0] || !load_tooldb(p, &D, err, sizeof err)) fprintf(stderr, "%s\n", err); }
    memset(t, 0, sizeof *t);
    int found = 0;
    for (int i = 0; i < D.n && !found; i++) if (D.t[i].type == type && D.t[i].is_metric == (metric != 0)) { tool_copy(t, &D.t[i]); found = 1; }
    t->type = type; t->num = num;
    if (dia > 0) { t->dia = dia; if (type == 11) t->corner_r = dia / 2; }
    return found;
}


/* ---------- legacy records: op_tool_info (0x288) and tp_tool (0x698, a TL_ID body) ---------- */
/* decomp/mccore_tooladd.c. The class a mill type makes: TlToolMill::CreateNewFromOldTool (mccore 0x180834f20) */
enum { TC_MILL, TC_CDRILL, TC_DRILL, TC_THREAD, TC_REAMER, TC_BORE, TC_CBORE, TC_ENDMILL, TC_SLOT, TC_FEED, TC_BARREL, TC_NONE };
static int tool_class(int type) {
    switch (type) {
    case 0: case 20: return TC_MILL;
    case 1: return TC_CDRILL;
    case 2: case 3: case 9: case 22: return TC_DRILL;
    case 4: case 5: case 24: return TC_THREAD;
    case 6: return TC_REAMER;
    case 7: return TC_BORE;
    case 8: return TC_CBORE;
    case 10: case 11: case 12: case 13: case 15: case 16: case 17: case 18: case 19: case 21: return TC_ENDMILL;
    case 14: return TC_SLOT;
    case 23: return TC_FEED;
    case 25: return TC_BARREL;
    default: return TC_NONE;
    }
}
static void wd8(unsigned char *b, double v) { memcpy(b, &v, 8); }
static void wi32(unsigned char *b, int32_t v) { memcpy(b, &v, 4); }
static void wi16(unsigned char *b, int16_t v) { memcpy(b, &v, 2); }
static void wstr(unsigned char *b, size_t n, const char *s) {   /* _mbsnbcpy_s(.., n, s, _TRUNCATE): bytes after the NUL kept */
    size_t k = strlen(s); if (k > n - 1) k = n - 1;
    memcpy(b, s, k); b[k] = 0;
}

void tool_op_info(const Tool *t, unsigned char *b) {
    /* TlTool::GetOpToolInfo 0x180754580: the stored block (TlTool +0x158), then name +0x56, slot +0x264, ToolStation
       +0x25c, ToolNumber +0, IsMetric +0x258, MachineGroup +0x274 (the caller's), DisplayType +0x26c, GeometryFile +0xce,
       GeometryLevel +0x270 (the last three: not in Tool, kept) */
    if (t->has_op_info) memcpy(b, t->op_info, 0x288); else memset(b, 0, 0x288);
    wstr(b + 0x56, 0x78, t->name);
    wi32(b + 0x264, t->slot);
    if (t->has_extras) wi32(b + 0x25c, t->head_num);
    wi32(b, t->num);
    b[0x258] = t->is_metric != 0;
    /* TlToolMill::GetOpToolInfo 0x18083ff20 */
    wd8(b + 0x08, t->dia);
    wi16(b + 0x52, (int16_t)t->flutes);
    wi16(b + 0x04, (int16_t)t->type);
    wi32(b + 0x2c, t->len_off); wi32(b + 0x28, t->dia_off);
    wd8(b + 0x30, t->feed); wd8(b + 0x38, t->plunge); wd8(b + 0x40, t->retract);
    wi32(b + 0x48, t->rpm);
    /* +0x50 TlCoolant IsOldStyle: the value read, else the Tool Manager's 16 Flood / 8 Off (toollib_net.md 6) */
    wi16(b + 0x50, (int16_t)(t->coolant_style ? t->coolant_style : t->coolant ? 16 : 8));
    wi16(b + 0x54, (int16_t)t->tool_mat);                               /* GetToolMaterialCode */
    switch (tool_class(t->type)) {
    case TC_ENDMILL: case TC_SLOT: {     /* TlToolEndmill::GetOpToolInfo 0x1807677b0 (SlotMill has none of its own) */
        wd8(b + 0x10, t->corner_r);
        wd8(b + 0x20, t->has_extras ? t->taper_angle : t->tip_angle);   /* +0x968 TaperAngle (part_tools.md 4) */
        if (t->has_extras) {             /* TlRadiusType 0 / 1 -> 0, 2 / 4 -> 1, 3 -> 2 */
            int r = t->radius_type;
            if (r == 0 || r == 1) wi16(b + 6, 0); else if (r == 2 || r == 4) wi16(b + 6, 1); else if (r == 3) wi16(b + 6, 2);
        } else wi16(b + 6, (int16_t)t->rad_type);
        break;
    }
    case TC_DRILL: case TC_CDRILL: case TC_CBORE: case TC_BORE:   /* TlToolDrill::GetOpToolInfo 0x18075ece0 */
        wd8(b + 0x10, t->corner_r); wd8(b + 0x20, t->tip_angle);
        if (tool_class(t->type) == TC_BORE && t->has_extras) {          /* TlToolBoring 0x18075a750 */
            wd8(b + 0x20, t->taper_angle);
            if (t->radius_type >= 0 && t->radius_type <= 2) wi16(b + 6, (int16_t)t->radius_type);
        }
        break;
    case TC_THREAD:                      /* TlToolThreading::GetOpToolInfo 0x18085f2d0: inch pitch stored as 1 / pitch */
        wd8(b + 0x18, t->is_metric || t->pitch <= 1e-9 ? t->pitch : 1.0 / t->pitch);
        wd8(b + 0x10, t->corner_r); wd8(b + 0x20, t->tip_angle);
        if (t->has_extras && t->radius_type >= 0 && t->radius_type <= 2) wi16(b + 6, (int16_t)t->radius_type);
        break;
    case TC_FEED:                        /* TlToolFeedMill 0xa37700: +6 = 1, +0x20 UpperTaperAngle; +0x10 = GetProgramRadius
                                            (NOT DECODED: kept) */
        wi16(b + 6, 1); if (t->has_extras) wd8(b + 0x20, t->upper_taper);
        break;
    default: break;                      /* TlToolMill, Reamer: no override; BarrelMill +0x10: NOT DECODED (kept) */
    }
}

void tool_tp_tool(const Tool *t, unsigned char *tp) {
    unsigned char oi[0x288];
    tool_op_info(t, oi);
    /* TlTool::SetToOldTool 0x180755070: zero, op_tool_info, MfgToolCode +0x2d8 (its this[0x7e8] legacy copy: NOT DECODED
       when set, so not used) */
    memset(tp, 0, 0x698); memcpy(tp, oi, 0x288);
    wstr(tp + 0x2d8, 0x78, t->mfg_code);
    /* TlToolMill::SetToOldTool 0x1808420e0 */
    wd8(tp + 0x298, t->oal); wd8(tp + 0x2a0, t->shoulder_len); wd8(tp + 0x290, t->flute_len);
    wd8(tp + 0x2a8, t->arbor_dia); wd8(tp + 0x288, t->req_pilot_dia);
    wd8(tp + 0x2c8, t->sfm_adj); wd8(tp + 0x2d0, t->fpt_adj);
    /* +0x469 IsFBMTool, +0x468 IsAggregate: not in Tool (0); +0x2b0 / +0x2b8 legacy holder diameter / length
       (TlToolMill +0x880 / +0x878): not in a .tooldb, 0 -> on load TlMgr::Add gives the assembly SetupNewAssembly's
       default holder; +0x66e canned-text coolant (20 x i16) and the +0x3d0 block (TlToolMill +0x8f0): NOT DECODED, 0 */
    wstr(tp + 0x350, 0x78, t->chuck);
    wi16(tp + 0x2c0, (int16_t)(t->spindle_dir == 1));
    wd8(tp + 0x470, t->shoulder_dia); wd8(tp + 0x478, t->taper_len);
    int c = tool_class(t->type);
    /* the op params: TlMillingOpParams (FUN_1807626d0) or TlHolemakingOpParams (FUN_18075c320) */
    int hole = c == TC_DRILL || c == TC_CDRILL || c == TC_CBORE || c == TC_BORE || c == TC_REAMER || (c == TC_THREAD && t->type != 24);
    if (hole) {
        wi16(tp + 0x3d0, (int16_t)t->cycle); wd8(tp + 0x3d8, t->peck1); wd8(tp + 0x3e0, t->peck2);
        wd8(tp + 0x3e8, t->peck_clear); wd8(tp + 0x3f0, t->chipbreak); wd8(tp + 0x3f8, t->dwell);
    } else if (c != TC_MILL) {
        wd8(tp + 0x3d8, t->step_rxy); wd8(tp + 0x3e0, t->step_rz); wd8(tp + 0x3e8, t->step_fxy); wd8(tp + 0x3f0, t->step_fz);
        wi16(tp + 0x3d0, (int16_t)(!t->is_rough ? 2 : !t->is_finish ? 1 : 0));
    }
    switch (c) {                         /* the subclasses' SetToOldTool */
    case TC_ENDMILL: case TC_SLOT: wd8(tp + 0x3f8, t->tip_dia); break;             /* +0x970 TipDiameter */
    case TC_FEED: wd8(tp + 0x3f8, t->tip_dia); break;                              /* +0x968 TipDiameter */
    case TC_DRILL: case TC_CDRILL: case TC_CBORE: case TC_BORE:
        wd8(tp + 0x400, t->shoulder_angle); wd8(tp + 0x408, 0);                    /* TlToolDrill 0x18075f2a0 */
        if (c == TC_BORE) wd8(tp + 0x410, t->shift);                               /* TlToolBoring 0x18075aa90 */
        break;
    case TC_REAMER: wd8(tp + 0x400, t->tip_angle); break;                          /* TlToolReamer 0x1808555f0: +0x968 */
    case TC_THREAD:                                                                /* TlToolThreading 0x18085fc30: +0x978 */
        wd8(tp + (t->type == 24 ? 0x400 : 0x408), t->root_dia); break;
    default: break;
    }
}

/* ---------- outline (side profile, for drawing the tool in the view) ---------- */
/* the parametric profile (toolprofile.c = tlcore TlProfileBuilderParametric, formats/tool_profiles.md). When it fails
   (build_auto_mill_tool error) backplotbnci FUN_67d16db0 shows a single line of the tool diameter at height tool_len
   (tool_profiles.md 5.1): here (0, OAL) -> (D/2, OAL). A holder is stacked with its face at z = tool_len above the tip
   (legacy FUN_67d18a90, 5.2); tool_len is taken as the overall length - how Mastercam's tool_len relates to the
   assembly stick-out is NOT DECODED. */
int tool_outline(const Tool *t, double *r, double *z, int max) {
    TpParams p; TpSeg seg[256]; int ns = 0, n;
    tp_params_from_tool(t, &p);
    if (tp_build(&p, seg, 256, &ns) == TP_OK && ns > 0) n = tp_polyline(seg, ns, r, z, max, M_PI / 16);
    else {
        if (max < 2) return 0;
        r[0] = 0; z[0] = t->oal > 0 ? t->oal : 0; r[1] = t->dia > 0 ? t->dia / 2 : 0; z[1] = z[0]; n = 2;
    }
    if (t->holder_n && t->holder_rz && n > 0) {
        double top = t->oal > 0 ? t->oal : z[n - 1];
        if (n > 1 && r[n - 1] < 1e-12) n--;                 /* drop the closing (0, OAL): the holder continues the outline */
        for (int i = 0; i < t->holder_n && n < max; i++) { r[n] = t->holder_rz[2 * i]; z[n] = top + t->holder_rz[2 * i + 1]; n++; }
        if (n < max && r[n - 1] > 1e-12) { r[n] = 0; z[n] = z[n - 1]; n++; }
    }
    return n;
}

/* ---------- library container ---------- */
int toollib_add(ToolLib *L, const Tool *t) {
    L->t = xrealloc(L->t, sizeof(Tool) * (L->n + 1));
    tool_copy(&L->t[L->n], t);
    L->t[L->n].has_guid = 0;                              /* a new row on save (the loader sets the GUID of read tools) */
    return L->n++;
}

void toollib_remove(ToolLib *L, int i) {
    if (i < 0 || i >= L->n) return;
    if (L->t[i].has_guid) { L->del = xrealloc(L->del, sizeof *L->del * (L->ndel + 1)); memcpy(L->del[L->ndel++], L->t[i].guid, 16); }
    free(L->t[i].holder_rz);
    memmove(L->t + i, L->t + i + 1, sizeof(Tool) * (L->n - i - 1));
    L->n--;
}

void toollib_free(ToolLib *L) {
    for (int i = 0; i < L->n; i++) free(L->t[i].holder_rz);
    for (int i = 0; i < L->nh; i++) free(L->h[i].rz);
    free(L->t); free(L->h); free(L->del);
    memset(L, 0, sizeof *L);
}

/* ---------- .tooldb (SQLite) ---------- */
static double col_d(sqlite3_stmt *s, int i) { return sqlite3_column_type(s, i) == SQLITE_NULL ? 0 : sqlite3_column_double(s, i); }
static int col_i(sqlite3_stmt *s, int i) { return sqlite3_column_type(s, i) == SQLITE_NULL ? 0 : sqlite3_column_int(s, i); }
static double rdd(const uint8_t *b) { double d; memcpy(&d, b, 8); return d; }
static int32_t rd32(const uint8_t *b) { int32_t v; memcpy(&v, b, 4); return v; }

static const char *Q_TOOLS =
    "select a.Name, t.ToolNumber, t.OpToolInfo, m.MCToolType, m.OverallDiameter, m.CuttingDepth, m.ShoulderLength,"
    " m.OverallLength, m.ShoulderDiameter, m.ArborDiameter, m.FluteCount, m.DiameterOffsetNum, m.LengthOffsetNum,"
    " coalesce(e.CornerRadius, th.CornerRadius, dr.CornerRadius), coalesce(dr.TipAngle, th.TipAngle, rm.TipAngle),"
    " th.ThreadPitch, coalesce(e.TipDiameter, th.TipDiameter, 0), o.SpindleSpeed, o.FeedRate, o.PlungeRate, o.RetractRate, c.OldStyle, c.Mist, c.Tool,"
    " h.CannedCycleType, h.Peck1, h.Peck2, h.ChipBreak, h.Dwell, a.IsMetric, a.ID"
    " from TlTool t join TlAssemblyItem a on a.ID = t.ID join TlToolMill m on m.ID = t.ID"
    " left join TlToolEndmill e on e.ID = t.ID left join TlToolDrill dr on dr.ID = t.ID"
    " left join TlToolThreading th on th.ID = t.ID left join TlToolReamer rm on rm.ID = t.ID"
    " left join TlOpParams o on o.ID = m.TlOpParamsID left join TlCoolant c on c.ID = o.TlCoolantID"
    " left join TlHolemakingOpParams h on h.ID = o.ID order by t.ToolNumber, a.Name";

/* the wizard's per-type columns (toolmanager_ui.md 2.6 / 2.9; mcam_load.h names each one). A separate query, so a
   library whose schema lacks one of them still loads its tools (with these left 0). */
static const char *Q_EXTRA =
    "select a.ID, m.TlShankType, m.TaperLength, m.NeckDiameter, m.RequiredPilotDiameter, m.HelixAngle, m.TlHelixType,"
    " m.TlSpindleDir, m.MfgToolCode, m.Chuck, t.ToolStation, a.Description,"
    " coalesce(e.TlRadiusType, bo.TlRadiusType, th.TlRadiusType, 0), coalesce(e.TaperAngle, bo.TaperAngle, 0),"
    " coalesce(e.ChamferLength, rm.ChamferLength, 0), sl.SecondaryRadiusType, sl.SecondaryCornerRadius,"
    " sl.SecondaryChamferDistance, sl.SecondaryChamferAngle, dr.ShoulderAngle, cd.DrillDiameter, cd.DrillLength,"
    " cb.PilotLength, cb.PilotDiameter, bo.Shift, th.TlThreadShape, th.NStarts, th.RootDiameter, th.ThreadAngle,"
    " ba.BarrelLength, ba.ProfileRadius, fm.LowerRadius, fm.UpperRadius, fm.LowerTaperAngle, fm.UpperTaperAngle,"
    " fm.ReliefTaperAngle, o.MaterialSFMAdjust, o.MaterialFPTAdjust, h.PeckClear, mo.IsRough, mo.IsFinish,"
    " mo.StepoverRoughXY, mo.StepoverRoughZ, mo.StepoverFinishXY, mo.StepoverFinishZ, mat.Name, ba.CornerRadius, fm.TipDiameter,"
    " t.ToolStation is null"
    " from TlTool t join TlAssemblyItem a on a.ID = t.ID join TlToolMill m on m.ID = t.ID"
    " left join TlToolEndmill e on e.ID = t.ID left join TlToolDrill dr on dr.ID = t.ID"
    " left join TlToolThreading th on th.ID = t.ID left join TlToolReamer rm on rm.ID = t.ID"
    " left join TlToolBoring bo on bo.ID = t.ID left join TlToolSlotMill sl on sl.ID = t.ID"
    " left join TlToolCenterDrill cd on cd.ID = t.ID left join TlToolCounterBore cb on cb.ID = t.ID"
    " left join TlToolBarrelMill ba on ba.ID = t.ID left join TlToolFeedMill fm on fm.ID = t.ID"
    " left join TlOpParams o on o.ID = m.TlOpParamsID left join TlHolemakingOpParams h on h.ID = o.ID"
    " left join TlMillingOpParams mo on mo.ID = o.ID left join TlMaterial mat on mat.ID = a.TlToolMaterialID";

static void col_s(sqlite3_stmt *s, int i, char *out, size_t n) { const unsigned char *v = sqlite3_column_text(s, i); snprintf(out, n, "%s", v ? (const char *)v : ""); }

static void load_extras(sqlite3 *db, ToolLib *L) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(db, Q_EXTRA, -1, &s, NULL) != SQLITE_OK) { fprintf(stderr, "tool library: wizard columns not read (%s)\n", sqlite3_errmsg(db)); return; }
    while (sqlite3_step(s) == SQLITE_ROW) {
        const void *g = sqlite3_column_blob(s, 0); if (!g || sqlite3_column_bytes(s, 0) != 16) continue;
        Tool *t = NULL; for (int i = 0; i < L->n && !t; i++) if (L->t[i].has_guid && !memcmp(L->t[i].guid, g, 16)) t = &L->t[i];
        if (!t) continue;
        t->has_extras = 1;
        t->shank_type = col_i(s, 1); t->taper_len = col_d(s, 2); t->neck_dia = col_d(s, 3); t->req_pilot_dia = col_d(s, 4);
        t->helix_angle = col_d(s, 5); t->helix_type = col_i(s, 6); t->spindle_dir = col_i(s, 7);
        col_s(s, 8, t->mfg_code, sizeof t->mfg_code); col_s(s, 9, t->chuck, sizeof t->chuck);
        t->head_num = col_i(s, 48) ? -1 : col_i(s, 10);                 /* ToolStation.GetValueOrDefault(-1) (TlToolViewModel.HeadNumber) */
        col_s(s, 11, t->desc, sizeof t->desc);
        t->radius_type = col_i(s, 12); t->taper_angle = col_d(s, 13); t->chamfer_len = col_d(s, 14);
        t->sec_radius_type = col_i(s, 15); t->sec_corner_r = col_d(s, 16); t->sec_chamfer = col_d(s, 17); t->sec_chamfer_angle = col_d(s, 18);
        t->shoulder_angle = col_d(s, 19); t->drill_dia = col_d(s, 20); t->drill_len = col_d(s, 21);
        t->pilot_len = col_d(s, 22); t->pilot_dia = col_d(s, 23); t->shift = col_d(s, 24);
        t->thread_shape = col_i(s, 25); t->nstarts = col_i(s, 26); t->root_dia = col_d(s, 27); t->thread_angle = col_d(s, 28);
        t->barrel_len = col_d(s, 29); t->profile_r = col_d(s, 30);
        t->lower_r = col_d(s, 31); t->upper_r = col_d(s, 32); t->lower_taper = col_d(s, 33); t->upper_taper = col_d(s, 34); t->relief_taper = col_d(s, 35);
        t->sfm_adj = col_d(s, 36); t->fpt_adj = col_d(s, 37); t->peck_clear = col_d(s, 38);
        t->is_rough = col_i(s, 39); t->is_finish = col_i(s, 40);
        t->step_rxy = col_d(s, 41); t->step_rz = col_d(s, 42); t->step_fxy = col_d(s, 43); t->step_fz = col_d(s, 44);
        col_s(s, 45, t->mat_name, sizeof t->mat_name);
        if (t->type == 25) t->corner_r = col_d(s, 46);                  /* TlToolBarrelMill.CornerRadius */
        if (t->type == 23) t->tip_dia = col_d(s, 47);                   /* TlToolFeedMill.TipDiameter */
    }
    sqlite3_finalize(s);
}

static int load_tooldb(const char *path, ToolLib *L, char *err, int errn) {
    sqlite3 *db;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        snprintf(err, errn, "cannot open %s: %s", path, sqlite3_errmsg(db)); sqlite3_close(db); return 0;
    }
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(db, Q_TOOLS, -1, &s, NULL) != SQLITE_OK) {
        snprintf(err, errn, "not a mill tool library (%s)", sqlite3_errmsg(db)); sqlite3_close(db); return 0;
    }
    int metric_votes = 0, rc, dberr = 0;               /* a step that ends in anything but DONE = a damaged library */
    while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
        Tool t; memset(&t, 0, sizeof t);
        const unsigned char *nm = sqlite3_column_text(s, 0);
        snprintf(t.name, sizeof t.name, "%s", nm ? (const char *)nm : "");
        t.num = col_i(s, 1); t.type = col_i(s, 3); t.dia = col_d(s, 4);
        t.flute_len = col_d(s, 5); t.shoulder_len = col_d(s, 6); t.oal = col_d(s, 7);
        t.shoulder_dia = col_d(s, 8); t.arbor_dia = col_d(s, 9); t.flutes = col_i(s, 10);
        t.dia_off = col_i(s, 11); t.len_off = col_i(s, 12);
        t.corner_r = col_d(s, 13); t.tip_angle = col_d(s, 14); t.pitch = col_d(s, 15);
        t.tip_dia = col_d(s, 16);
        t.rpm = col_i(s, 17); t.feed = col_d(s, 18); t.plunge = col_d(s, 19); t.retract = col_d(s, 20);
        t.coolant = col_i(s, 21) == 16; t.coolant_style = col_i(s, 21);   /* the raw value is written back unchanged */                 /* TlCoolant.OldStyle: 8 off, 16 flood, 32 mist, 64 thru (toollib_net.md 6) */
        { const void *g = sqlite3_column_blob(s, 30); if (g && sqlite3_column_bytes(s, 30) == 16) { memcpy(t.guid, g, 16); t.has_guid = 1; } }
        t.cycle = col_i(s, 24); t.peck1 = col_d(s, 25); t.peck2 = col_d(s, 26); t.chipbreak = col_d(s, 27); t.dwell = col_d(s, 28);
        t.is_metric = col_i(s, 29) != 0;
        metric_votes += col_i(s, 29) ? 1 : -1;
        const uint8_t *b = sqlite3_column_blob(s, 2); int bl = sqlite3_column_bytes(s, 2);
        if (b && bl == 0x288) { memcpy(t.op_info, b, 0x288); t.has_op_info = 1; }   /* ConvertExtensionsTool takes it only at 648 bytes (toollib_net.md 3.5) */
        if (b && bl >= 0x50) {                    /* the op-facing record fills what the tables leave out */
            if (!t.tip_angle) t.tip_angle = rdd(b + 0x20);
            t.rad_type = (int16_t)(b[6] | b[7] << 8);
            if (!t.corner_r) t.corner_r = rdd(b + 0x10);
            if (!t.pitch) t.pitch = rdd(b + 0x18);
            if (!t.rpm) t.rpm = rd32(b + 0x48);
            if (bl >= 0x56) t.tool_mat = (int16_t)(b[0x54] | b[0x55] << 8);   /* cutter material (feeds_speeds.md) */
        }
        if (t.type == 11 && t.corner_r <= 0) t.corner_r = t.dia / 2;
        { int k = toollib_add(L, &t); L->t[k].has_guid = t.has_guid; memcpy(L->t[k].guid, t.guid, 16); }
    }
    if (rc != SQLITE_DONE) dberr = 1;
    sqlite3_finalize(s);
    if (!dberr) load_extras(db, L);
    /* holders and their outlines (TlProfileData: line segments x = radius, y = height) */
    /* TlProfileData: Type 2 a line (x0, y0) -> (x1, y1); any other type an arc, centre (x0, y0), radius, StartAngle /
       SweepAngle in radians (toollib_net.md 4.1), taken as 100 lines as tlcore TlProfileArcSegment::LinearizeArc does */
    if (sqlite3_prepare_v2(db, "select a.Name, p.x0, p.y0, p.x1, p.y1, p.ItemID, p.Type, p.radius, p.StartAngle, p.SweepAngle from TlHolder h join TlAssemblyItem a on a.ID = h.ID"
                           " join TlProfileData p on p.ItemID = h.ID order by a.Name, p.ItemID, p.Segment", -1, &s, NULL) == SQLITE_OK) {
        const void *cur = NULL; int curlen = 0; uint8_t curid[16];
        while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
            const void *id = sqlite3_column_blob(s, 5); int il = sqlite3_column_bytes(s, 5);
            if (!cur || il != curlen || memcmp(id, curid, il > 16 ? 16 : il)) {
                L->h = xrealloc(L->h, sizeof(Holder) * (L->nh + 1));
                Holder *h = &L->h[L->nh++]; memset(h, 0, sizeof *h);
                const unsigned char *nm = sqlite3_column_text(s, 0);
                snprintf(h->name, sizeof h->name, "%s", nm ? (const char *)nm : "");
                memcpy(curid, id, il > 16 ? 16 : il); curlen = il; cur = curid;
                h->rz = xmalloc(sizeof(double) * 2); h->n = 1;
                if (col_i(s, 6) == 2) { h->rz[0] = col_d(s, 1); h->rz[1] = col_d(s, 2); }
                else { h->rz[0] = col_d(s, 1) + col_d(s, 7) * cos(col_d(s, 8)); h->rz[1] = col_d(s, 2) + col_d(s, 7) * sin(col_d(s, 8)); }
            }
            Holder *h = &L->h[L->nh - 1];
            if (col_i(s, 6) == 2) {
                h->rz = xrealloc(h->rz, sizeof(double) * 2 * (h->n + 1));
                h->rz[2 * h->n] = col_d(s, 3); h->rz[2 * h->n + 1] = col_d(s, 4); h->n++;
            } else {
                double cx = col_d(s, 1), cy = col_d(s, 2), r = col_d(s, 7), a0 = col_d(s, 8), sw = col_d(s, 9);
                h->rz = xrealloc(h->rz, sizeof(double) * 2 * (h->n + 100));
                for (int k = 1; k <= 100; k++) { h->rz[2 * h->n] = cx + r * cos(a0 + k * sw / 100.0); h->rz[2 * h->n + 1] = cy + r * sin(a0 + k * sw / 100.0); h->n++; }
            }
        }
        if (rc != SQLITE_DONE) dberr = 1; sqlite3_finalize(s);
    }
    if (!metric_votes && sqlite3_prepare_v2(db, "select sum(IsMetric), count(*) from TlAssemblyItem", -1, &s, NULL) == SQLITE_OK) {
        if (sqlite3_step(s) == SQLITE_ROW) metric_votes = 2 * col_i(s, 0) - col_i(s, 1);
        sqlite3_finalize(s);
    }
    sqlite3_close(db);
    L->metric = metric_votes > 0 ? 1 : metric_votes < 0 ? 0 : -1;
    if (dberr) { snprintf(err, errn, "%s is damaged or locked (%s) - not loaded", path, "reading stopped before the end"); toollib_free(L); return 0; }
    if (!L->n && !L->nh) { snprintf(err, errn, "no mill tools or holders in %s", path); return 0; }
    return 1;
}

/* ---------- .tools text format ---------- */
#define TOOL_FIELDS(X) \
    X(num, "%d", i) X(type, "%d", i) X(dia, "%.17g", d) X(corner_r, "%.17g", d) X(tip_angle, "%.17g", d) X(pitch, "%.17g", d) \
    X(tip_dia, "%.17g", d) \
    X(dia_off, "%d", i) X(len_off, "%d", i) X(rpm, "%d", i) X(feed, "%.17g", d) X(plunge, "%.17g", d) X(retract, "%.17g", d) \
    X(flutes, "%d", i) X(flute_len, "%.17g", d) X(shoulder_len, "%.17g", d) X(oal, "%.17g", d) X(shoulder_dia, "%.17g", d) \
    X(arbor_dia, "%.17g", d) X(coolant, "%d", i) X(cycle, "%d", i) X(peck1, "%.17g", d) X(peck2, "%.17g", d) \
    X(chipbreak, "%.17g", d) X(dwell, "%.17g", d) X(tool_mat, "%d", i) \
    X(is_metric, "%d", i) X(coolant_style, "%d", i) X(rad_type, "%d", i) X(has_extras, "%d", i) X(radius_type, "%d", i) X(taper_angle, "%.17g", d) X(chamfer_len, "%.17g", d) \
    X(shank_type, "%d", i) X(taper_len, "%.17g", d) X(neck_dia, "%.17g", d) X(sec_radius_type, "%d", i) \
    X(sec_corner_r, "%.17g", d) X(sec_chamfer, "%.17g", d) X(sec_chamfer_angle, "%.17g", d) X(shoulder_angle, "%.17g", d) \
    X(drill_dia, "%.17g", d) X(drill_len, "%.17g", d) X(pilot_len, "%.17g", d) X(pilot_dia, "%.17g", d) X(shift, "%.17g", d) \
    X(thread_shape, "%d", i) X(nstarts, "%d", i) X(root_dia, "%.17g", d) X(thread_angle, "%.17g", d) \
    X(barrel_len, "%.17g", d) X(profile_r, "%.17g", d) X(lower_r, "%.17g", d) X(upper_r, "%.17g", d) \
    X(lower_taper, "%.17g", d) X(upper_taper, "%.17g", d) X(relief_taper, "%.17g", d) X(req_pilot_dia, "%.17g", d) \
    X(helix_angle, "%.17g", d) X(helix_type, "%d", i) X(spindle_dir, "%d", i) X(head_num, "%d", i) \
    X(sfm_adj, "%.17g", d) X(fpt_adj, "%.17g", d) X(peck_clear, "%.17g", d) X(is_rough, "%d", i) X(is_finish, "%d", i) \
    X(step_rxy, "%.17g", d) X(step_rz, "%.17g", d) X(step_fxy, "%.17g", d) X(step_fz, "%.17g", d) \
    X(slot, "%d", i)
/* text fields before the name: %XX-escaped so they hold no spaces */
#define TOOL_STRS(X) X(desc) X(mfg_code) X(chuck) X(mat_name)
static void put_esc(FILE *o, const char *k, const char *v) {
    if (!v[0]) return;
    fprintf(o, " %s=", k);
    for (const unsigned char *p = (const unsigned char *)v; *p; p++) if (*p <= ' ' || *p == '%' || *p == '=' || *p >= 0x7f) fprintf(o, "%%%02X", *p); else fputc(*p, o);
}
static void get_esc(char *out, size_t n, const char *v) {
    size_t k = 0;
    for (; *v && k + 1 < n; v++) {
        if (*v == '%' && v[1] && v[2]) { char h[3] = { v[1], v[2], 0 }; out[k++] = (char)strtol(h, NULL, 16); v += 2; } else out[k++] = *v;
    }
    out[k] = 0;
}

static void write_holder(FILE *o, const char *tag, const char *name, const double *rz, int n) {
    fprintf(o, "%s %d", tag, n);
    for (int i = 0; i < 2 * n; i++) fprintf(o, " %.17g", rz[i]);
    fprintf(o, " %s\n", name);
}

void tool_write(FILE *o, const Tool *t) {
    fprintf(o, "TOOL");
#define W(f, fmt, k) fprintf(o, " " #f "=" fmt, t->f);
    TOOL_FIELDS(W)
#undef W
#define S(f) put_esc(o, #f, t->f);
    TOOL_STRS(S)
#undef S
    if (t->has_op_info) {                                    /* the op_tool_info block (mcam_load.h), hex */
        fprintf(o, " opinfo=");
        for (int i = 0; i < 0x288; i++) fprintf(o, "%02x", t->op_info[i]);
    }
    fprintf(o, " name=%s\n", t->name);
    if (t->holder_n) write_holder(o, "THOLDER", t->holder, t->holder_rz, t->holder_n);
}

int toollib_save(const char *path, const ToolLib *L) {
    char tmp[4200]; if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return 0;   /* never a truncated name */
    FILE *o = fopen(tmp, "w");
    if (!o) return 0;
    fprintf(o, "MCAMUI-TOOLS 1\nUNITS %s\n", L->metric == 0 ? "inch" : "mm");
    for (int i = 0; i < L->n; i++) tool_write(o, &L->t[i]);
    for (int i = 0; i < L->nh; i++) write_holder(o, "HOLDER", L->h[i].name, L->h[i].rz, L->h[i].n);
    int ok = !ferror(o);
    if (fclose(o) != 0) ok = 0;
    if (!ok || file_replace(tmp, path) != 0) { remove(tmp); return 0; }
    return 1;
}

static int read_holder(char *p, char *name, int namen, double **rz) {
    int n = (int)strtol(p, &p, 10);
    if (n <= 0 || n > 100000) return 0;
    *rz = xmalloc(sizeof(double) * 2 * n);
    for (int i = 0; i < 2 * n; i++) (*rz)[i] = strtod(p, &p);
    while (*p == ' ') p++;
    snprintf(name, namen, "%s", p);
    return n;
}

int tool_parse(const char *line, Tool *t) {
    memset(t, 0, sizeof *t);
    if (strncmp(line, "TOOL ", 5)) return 0;
    char buf[16384]; snprintf(buf, sizeof buf, "%s", line + 5);
    char *nm = strstr(buf, " name=");                        /* the name is last and may contain spaces */
    if (nm) { snprintf(t->name, sizeof t->name, "%s", nm + 6); *nm = 0; }
    char *save = NULL;
    for (char *k = strtok_r(buf, " ", &save); k; k = strtok_r(NULL, " ", &save)) {
        char *eq = strchr(k, '='); if (!eq) continue; *eq = 0; const char *v = eq + 1;
        if (!strcmp(k, "opinfo")) {
            if (strlen(v) == 2 * 0x288) {
                t->has_op_info = 1;
                for (int i = 0; i < 0x288; i++) { char h[3] = { v[2 * i], v[2 * i + 1], 0 }; t->op_info[i] = (unsigned char)strtol(h, NULL, 16); }
            }
            continue;
        }
#define S(f) if (!strcmp(k, #f)) { get_esc(t->f, sizeof t->f, v); continue; }
        TOOL_STRS(S)
#undef S
        double dv = atof(v); if (!isfinite(dv) || fabs(dv) > 1e9) continue;   /* NaN / absurd values are not taken */
#define R(fl, fmt, kind) if (!strcmp(k, #fl)) t->fl = dv;   /* ints are written as integers */
        TOOL_FIELDS(R)
#undef R
    }
    if (!(t->dia > 0) || !isfinite(t->dia)) return 0;
    return 1;
}

int tool_read_holder(const char *line, Tool *t) {        /* "THOLDER n r z ... name" after a TOOL line */
    if (strncmp(line, "THOLDER ", 8)) return 0;
    char buf[1 << 16]; snprintf(buf, sizeof buf, "%s", line + 8);
    free(t->holder_rz); t->holder_rz = NULL;
    t->holder_n = read_holder(buf, t->holder, sizeof t->holder, &t->holder_rz);
    return t->holder_n > 0;
}

static int load_text(const char *path, ToolLib *L, char *err, int errn) {
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, errn, "cannot open %s", path); return 0; }
    static char line[1 << 20];
    if (!fgets(line, sizeof line, f) || strncmp(line, "MCAMUI-TOOLS", 12)) { fclose(f); snprintf(err, errn, "%s: not a .tools file", path); return 0; }
    L->metric = 1;
    int base = 0;                                    /* "BASE defaults": each tool starts as the type's default (tool_new) */
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "UNITS ", 6)) L->metric = strcmp(line + 6, "inch") != 0;
        else if (!strcmp(line, "BASE defaults")) base = 1;
        else if (!strncmp(line, "TOOL ", 5)) {
            Tool t;
            if (tool_parse(line, &t)) {
                if (!strstr(line, " is_metric=")) t.is_metric = L->metric == 1;   /* files written before is_metric was stored */
                if (base) {                          /* the starter library (ui/starter_tools_mm.tools): the line holds the
                                                        geometry only; speeds, holder and the rest come from the type's
                                                        default tool, as a new tool in the Tool Manager */
                    Tool d; tool_new(&d, t.type, t.dia, t.num, t.is_metric);
#define K(fl, fmt, kind) { char k_[40]; snprintf(k_, sizeof k_, " " #fl "="); if (strstr(line, k_)) d.fl = t.fl; }
                    TOOL_FIELDS(K)
#undef K
                    snprintf(d.name, sizeof d.name, "%s", t.name);
                    if (t.desc[0]) snprintf(d.desc, sizeof d.desc, "%s", t.desc);
                    d.is_metric = t.is_metric;
                    toollib_add(L, &d); free(d.holder_rz);
                } else toollib_add(L, &t);
            }
        } else if (!strncmp(line, "THOLDER ", 8) && L->n) {
            Tool *t = &L->t[L->n - 1];
            t->holder_n = read_holder(line + 8, t->holder, sizeof t->holder, &t->holder_rz);
        } else if (!strncmp(line, "HOLDER ", 7)) {
            Holder h; memset(&h, 0, sizeof h);
            if ((h.n = read_holder(line + 7, h.name, sizeof h.name, &h.rz))) {
                L->h = xrealloc(L->h, sizeof(Holder) * (L->nh + 1)); L->h[L->nh++] = h;
            }
        }
    }
    fclose(f);
    return 1;
}

int toollib_load(const char *path, ToolLib *L, char *err, int errn) {
    memset(L, 0, sizeof *L);
    snprintf(L->path, sizeof L->path, "%s", path);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, errn, "cannot open %s", path); return 0; }
    char magic[16] = { 0 }; size_t got = fread(magic, 1, 16, f); fclose(f);
    int ok = got >= 15 && !memcmp(magic, "SQLite format 3", 15) ? load_tooldb(path, L, err, errn) : load_text(path, L, err, errn);
    if (!ok) { toollib_free(L); return 0; }
    snprintf(L->path, sizeof L->path, "%s", path);
    return 1;
}

/* ---------- finding libraries ---------- */
static int ends_with(const char *s, const char *e) { size_t a = strlen(s), b = strlen(e); return a >= b && !strcasecmp(s + a - b, e); }

static void scan_dir(const char *dir, char ***paths, int *n) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!ends_with(e->d_name, ".tooldb") && !ends_with(e->d_name, ".tools")) continue;
        *paths = xrealloc(*paths, sizeof(char *) * (*n + 1));
        size_t len = strlen(dir) + strlen(e->d_name) + 2;
        (*paths)[*n] = xmalloc(len); path_join((*paths)[*n], len, dir, e->d_name); (*n)++;
    }
    closedir(d);
}

static int cmp_str(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

int toollib_find(char ***paths) {
    int n = 0; *paths = NULL;
    const char *env = getenv("MCAM_TOOLLIB_DIR");
    if (env) scan_dir(env, paths, &n);
    scan_dir("tools", paths, &n);
    scan_dir(config_dir(), paths, &n);                    /* platform.h */
    int user = n;
    static const char *mc[] = { "mill/Tools", "router/Tools" };
    for (int i = 0; i < 2; i++) { char d[1100]; if (site_file(d, sizeof d, "shared", mc[i])[0]) scan_dir(d, paths, &n); }
    qsort(*paths, user, sizeof(char *), cmp_str);
    qsort(*paths + user, n - user, sizeof(char *), cmp_str);
    return n;
}

/* ---------- .tooldb writer (formats/toollib_net.md section 3) ---------- */
/* new GUID in .NET Guid.ToByteArray() order (random, version 4 - Guid.NewGuid) */
static void new_guid(unsigned char g[16]) {
    static int seeded; if (!seeded) { srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)g); seeded = 1; }
    for (int i = 0; i < 16; i++) g[i] = (unsigned char)(rand() >> 7);
    g[7] = (unsigned char)((g[7] & 0x0f) | 0x40); g[8] = (unsigned char)((g[8] & 0x3f) | 0x80);
}
static int db_exec(sqlite3 *db, const char *sql, char *err, int errn) {
    char *e = NULL; if (sqlite3_exec(db, sql, NULL, NULL, &e) == SQLITE_OK) return 1;
    snprintf(err, errn, "%s (%s)", e ? e : "sqlite error", sql); sqlite3_free(e); return 0;
}
static int row_exists(sqlite3 *db, const char *schema, const char *table, const unsigned char *id) {
    char q[200]; snprintf(q, sizeof q, "select 1 from %s.%s where ID = ?", schema, table);
    sqlite3_stmt *s; if (sqlite3_prepare_v2(db, q, -1, &s, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_blob(s, 1, id, 16, SQLITE_STATIC); int r = sqlite3_step(s) == SQLITE_ROW; sqlite3_finalize(s); return r;
}
/* copy row ID of TABLE from the attached defaults DB into main under NEW_ID, columns by name (column order differs
   between libraries, toollib_net.md 2.1); FK columns in SUBST (name, new value) replaced */
static int copy_row(sqlite3 *db, const char *table, const unsigned char *id, const unsigned char *new_id,
                    const char *const *subst_col, const unsigned char *const *subst_val, int nsubst, char *err, int errn) {
    char q[4096], cols[3000] = "", sel[3000] = ""; sqlite3_stmt *s;
    snprintf(q, sizeof q, "pragma main.table_info(%s)", table);
    if (sqlite3_prepare_v2(db, q, -1, &s, NULL) != SQLITE_OK) { snprintf(err, errn, "%s", sqlite3_errmsg(db)); return 0; }
    int nparam = 0; const unsigned char *pv[16];
    while (sqlite3_step(s) == SQLITE_ROW) {
        const char *c = (const char *)sqlite3_column_text(s, 1); size_t l = strlen(cols);
        snprintf(cols + l, sizeof cols - l, "%s\"%s\"", l ? ", " : "", c);
        int sub = -1; for (int k = 0; k < nsubst; k++) if (!strcmp(subst_col[k], c)) sub = k;
        l = strlen(sel);
        if (!strcmp(c, "ID")) { snprintf(sel + l, sizeof sel - l, "%s?", l ? ", " : ""); pv[nparam++] = new_id; }
        else if (sub >= 0) { snprintf(sel + l, sizeof sel - l, "%s?", l ? ", " : ""); pv[nparam++] = subst_val[sub]; }
        else snprintf(sel + l, sizeof sel - l, "%s\"%s\"", l ? ", " : "", c);
    }
    sqlite3_finalize(s);
    snprintf(q, sizeof q, "insert into main.%s (%s) select %s from d.%s where ID = ?", table, cols, sel, table);
    if (sqlite3_prepare_v2(db, q, -1, &s, NULL) != SQLITE_OK) { snprintf(err, errn, "%s: %s", table, sqlite3_errmsg(db)); return 0; }
    for (int k = 0; k < nparam; k++) sqlite3_bind_blob(s, k + 1, pv[k], 16, SQLITE_STATIC);
    sqlite3_bind_blob(s, nparam + 1, id, 16, SQLITE_STATIC);
    int ok = sqlite3_step(s) == SQLITE_DONE; if (!ok) snprintf(err, errn, "%s: %s", table, sqlite3_errmsg(db));
    sqlite3_finalize(s); return ok;
}
static int get_guid(sqlite3 *db, const char *q, const unsigned char *id, unsigned char *out) {
    sqlite3_stmt *s; if (sqlite3_prepare_v2(db, q, -1, &s, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_blob(s, 1, id, 16, SQLITE_STATIC); int ok = 0;
    if (sqlite3_step(s) == SQLITE_ROW && sqlite3_column_bytes(s, 0) == 16) { memcpy(out, sqlite3_column_blob(s, 0), 16); ok = 1; }
    sqlite3_finalize(s); return ok;
}
static const char *TOOL_LEAVES[] = { "TlToolSlotMill", "TlToolEndmill", "TlToolBoring", "TlToolCounterBore", "TlToolCenterDrill",
                                      "TlToolDrill", "TlToolReamer", "TlToolThreading", "TlToolBarrelMill", "TlToolFeedMill" };
/* a reference row the copied tool points to (material / manufacturer / grade) copied from the defaults if missing
   (VerifyReference*, toollib_net.md 3.4) */
static int ensure_ref(sqlite3 *db, const char *const *tables, int nt, const unsigned char *id, char *err, int errn) {
    static const unsigned char zero[16];
    if (!memcmp(id, zero, 16)) return 1;
    for (int k = 0; k < nt; k++) if (!row_exists(db, "main", tables[k], id) && row_exists(db, "d", tables[k], id))
        if (!copy_row(db, tables[k], id, id, NULL, NULL, 0, err, errn)) return 0;
    return 1;
}
/* OldStyle to write: the value read (mist / thru / undecoded values kept) while it still agrees with the flood flag,
   else 16 flood / 8 off (toollib_net.md 6) */
static int tool_coolant_style(const Tool *t) {
    if (t->coolant_style && ((t->coolant_style == 16) == (t->coolant != 0))) return t->coolant_style;
    return t->coolant ? 16 : 8;
}
/* the columns this program edits (toollib_net.md 2.4), written for tool T (row ID, op params OP, coolant CO) */
static int write_cols(sqlite3 *db, const Tool *t, const unsigned char *id, const unsigned char *op, const unsigned char *co, char *err, int errn) {
    struct { const char *sql; int kind; double d; int i; const char *s; const unsigned char *who; } U[] = {
        { "update TlAssemblyItem set Name = ? where ID = ?", 2, 0, 0, t->name, id },
        { "update TlAssemblyItem set IsMetric = ? where ID = ?", 1, 0, t->is_metric != 0, 0, id },
        { "update TlTool set ToolNumber = ? where ID = ?", 1, 0, t->num, 0, id },
        { "update TlToolMill set MCToolType = ? where ID = ?", 1, 0, t->type, 0, id },
        { "update TlToolMill set OverallDiameter = ? where ID = ?", 0, t->dia, 0, 0, id },
        { "update TlToolMill set CuttingDepth = ? where ID = ?", 0, t->flute_len, 0, 0, id },
        { "update TlToolMill set ShoulderLength = ? where ID = ?", 0, t->shoulder_len, 0, 0, id },
        { "update TlToolMill set OverallLength = ? where ID = ?", 0, t->oal, 0, 0, id },
        { "update TlToolMill set ShoulderDiameter = ? where ID = ?", 0, t->shoulder_dia, 0, 0, id },
        { "update TlToolMill set ArborDiameter = ? where ID = ?", 0, t->arbor_dia, 0, 0, id },
        { "update TlToolMill set FluteCount = ? where ID = ?", 1, 0, t->flutes, 0, id },
        { "update TlToolMill set DiameterOffsetNum = ? where ID = ?", 1, 0, t->dia_off, 0, id },
        { "update TlToolMill set LengthOffsetNum = ? where ID = ?", 1, 0, t->len_off, 0, id },
        { "update TlToolEndmill set CornerRadius = ? where ID = ?", 0, t->corner_r, 0, 0, id },
        { "update TlToolEndmill set TipDiameter = ? where ID = ?", 0, t->tip_dia, 0, 0, id },
        { "update TlToolDrill set CornerRadius = ? where ID = ?", 0, t->corner_r, 0, 0, id },
        { "update TlToolDrill set TipAngle = ? where ID = ?", 0, t->tip_angle, 0, 0, id },
        { "update TlToolReamer set TipAngle = ? where ID = ?", 0, t->tip_angle, 0, 0, id },
        { "update TlToolThreading set CornerRadius = ? where ID = ?", 0, t->corner_r, 0, 0, id },
        { "update TlToolThreading set TipAngle = ? where ID = ?", 0, t->tip_angle, 0, 0, id },
        { "update TlToolThreading set ThreadPitch = ? where ID = ?", 0, t->pitch, 0, 0, id },
        { "update TlToolThreading set TipDiameter = ? where ID = ?", 0, t->tip_dia, 0, 0, id },
        { "update TlOpParams set SpindleSpeed = ? where ID = ?", 1, 0, t->rpm, 0, op },
        { "update TlOpParams set FeedRate = ? where ID = ?", 0, t->feed, 0, 0, op },
        { "update TlOpParams set PlungeRate = ? where ID = ?", 0, t->plunge, 0, 0, op },
        { "update TlOpParams set RetractRate = ? where ID = ?", 0, t->retract, 0, 0, op },
        { "update TlHolemakingOpParams set CannedCycleType = ? where ID = ?", 1, 0, t->cycle, 0, op },
        { "update TlHolemakingOpParams set Peck1 = ? where ID = ?", 0, t->peck1, 0, 0, op },
        { "update TlHolemakingOpParams set Peck2 = ? where ID = ?", 0, t->peck2, 0, 0, op },
        { "update TlHolemakingOpParams set ChipBreak = ? where ID = ?", 0, t->chipbreak, 0, 0, op },
        { "update TlHolemakingOpParams set Dwell = ? where ID = ?", 0, t->dwell, 0, 0, op },
        { "update TlCoolant set OldStyle = ? where ID = ?", 1, 0, tool_coolant_style(t), 0, co },   /* the value read, unless flood was toggled */
        /* the wizard's columns (load_extras): written only for a tool that has them (has_extras) */
        { "update TlAssemblyItem set Description = ? where ID = ?", 2, 0, 0, t->desc, id },
        { "update TlTool set ToolStation = ? where ID = ?", 3, 0, t->head_num, 0, id },
        { "update TlToolMill set TlShankType = ? where ID = ?", 1, 0, t->shank_type, 0, id },
        { "update TlToolMill set TaperLength = ? where ID = ?", 0, t->taper_len, 0, 0, id },
        { "update TlToolMill set NeckDiameter = ? where ID = ?", 0, t->neck_dia, 0, 0, id },
        { "update TlToolMill set RequiredPilotDiameter = ? where ID = ?", 0, t->req_pilot_dia, 0, 0, id },
        { "update TlToolMill set HelixAngle = ? where ID = ?", 0, t->helix_angle, 0, 0, id },
        { "update TlToolMill set TlHelixType = ? where ID = ?", 1, 0, t->helix_type, 0, id },
        { "update TlToolMill set TlSpindleDir = ? where ID = ?", 1, 0, t->spindle_dir, 0, id },
        { "update TlToolMill set MfgToolCode = ? where ID = ?", 2, 0, 0, t->mfg_code, id },
        { "update TlToolMill set Chuck = ? where ID = ?", 2, 0, 0, t->chuck, id },
        { "update TlToolEndmill set TlRadiusType = ? where ID = ?", 1, 0, t->radius_type, 0, id },
        { "update TlToolEndmill set TaperAngle = ? where ID = ?", 0, t->taper_angle, 0, 0, id },
        { "update TlToolEndmill set ChamferLength = ? where ID = ?", 0, t->chamfer_len, 0, 0, id },
        { "update TlToolSlotMill set SecondaryRadiusType = ? where ID = ?", 1, 0, t->sec_radius_type, 0, id },
        { "update TlToolSlotMill set SecondaryCornerRadius = ? where ID = ?", 0, t->sec_corner_r, 0, 0, id },
        { "update TlToolSlotMill set SecondaryChamferDistance = ? where ID = ?", 0, t->sec_chamfer, 0, 0, id },
        { "update TlToolSlotMill set SecondaryChamferAngle = ? where ID = ?", 0, t->sec_chamfer_angle, 0, 0, id },
        { "update TlToolDrill set ShoulderAngle = ? where ID = ?", 0, t->shoulder_angle, 0, 0, id },
        { "update TlToolCenterDrill set DrillDiameter = ? where ID = ?", 0, t->drill_dia, 0, 0, id },
        { "update TlToolCenterDrill set DrillLength = ? where ID = ?", 0, t->drill_len, 0, 0, id },
        { "update TlToolCounterBore set PilotLength = ? where ID = ?", 0, t->pilot_len, 0, 0, id },
        { "update TlToolCounterBore set PilotDiameter = ? where ID = ?", 0, t->pilot_dia, 0, 0, id },
        { "update TlToolBoring set TlRadiusType = ? where ID = ?", 1, 0, t->radius_type, 0, id },
        { "update TlToolBoring set TaperAngle = ? where ID = ?", 0, t->taper_angle, 0, 0, id },
        { "update TlToolBoring set Shift = ? where ID = ?", 0, t->shift, 0, 0, id },
        { "update TlToolReamer set ChamferLength = ? where ID = ?", 0, t->chamfer_len, 0, 0, id },
        { "update TlToolThreading set TlRadiusType = ? where ID = ?", 1, 0, t->radius_type, 0, id },
        { "update TlToolThreading set TlThreadShape = ? where ID = ?", 1, 0, t->thread_shape, 0, id },
        { "update TlToolThreading set NStarts = ? where ID = ?", 1, 0, t->nstarts, 0, id },
        { "update TlToolThreading set RootDiameter = ? where ID = ?", 0, t->root_dia, 0, 0, id },
        { "update TlToolThreading set ThreadAngle = ? where ID = ?", 0, t->thread_angle, 0, 0, id },
        { "update TlToolBarrelMill set BarrelLength = ? where ID = ?", 0, t->barrel_len, 0, 0, id },
        { "update TlToolBarrelMill set ProfileRadius = ? where ID = ?", 0, t->profile_r, 0, 0, id },
        { "update TlToolBarrelMill set CornerRadius = ? where ID = ?", 0, t->corner_r, 0, 0, id },
        { "update TlToolFeedMill set TipDiameter = ? where ID = ?", 0, t->tip_dia, 0, 0, id },
        { "update TlToolFeedMill set LowerRadius = ? where ID = ?", 0, t->lower_r, 0, 0, id },
        { "update TlToolFeedMill set UpperRadius = ? where ID = ?", 0, t->upper_r, 0, 0, id },
        { "update TlToolFeedMill set LowerTaperAngle = ? where ID = ?", 0, t->lower_taper, 0, 0, id },
        { "update TlToolFeedMill set UpperTaperAngle = ? where ID = ?", 0, t->upper_taper, 0, 0, id },
        { "update TlToolFeedMill set ReliefTaperAngle = ? where ID = ?", 0, t->relief_taper, 0, 0, id },
        { "update TlOpParams set MaterialSFMAdjust = ? where ID = ?", 0, t->sfm_adj, 0, 0, op },
        { "update TlOpParams set MaterialFPTAdjust = ? where ID = ?", 0, t->fpt_adj, 0, 0, op },
        { "update TlHolemakingOpParams set PeckClear = ? where ID = ?", 0, t->peck_clear, 0, 0, op },
        { "update TlMillingOpParams set IsRough = ? where ID = ?", 1, 0, t->is_rough != 0, 0, op },
        { "update TlMillingOpParams set IsFinish = ? where ID = ?", 1, 0, t->is_finish != 0, 0, op },
        { "update TlMillingOpParams set StepoverRoughXY = ? where ID = ?", 0, t->step_rxy, 0, 0, op },
        { "update TlMillingOpParams set StepoverRoughZ = ? where ID = ?", 0, t->step_rz, 0, 0, op },
        { "update TlMillingOpParams set StepoverFinishXY = ? where ID = ?", 0, t->step_fxy, 0, 0, op },
        { "update TlMillingOpParams set StepoverFinishZ = ? where ID = ?", 0, t->step_fz, 0, 0, op },
    };
    size_t first_extra = 0; while (strncmp(U[first_extra].sql, "update TlAssemblyItem set Description", 37)) first_extra++;
    for (size_t k = 0; k < sizeof U / sizeof U[0]; k++) {
        if (!U[k].who || (k >= first_extra && !t->has_extras)) continue;
        sqlite3_stmt *s; if (sqlite3_prepare_v2(db, U[k].sql, -1, &s, NULL) != SQLITE_OK) { snprintf(err, errn, "%s", sqlite3_errmsg(db)); return 0; }
        if (U[k].kind == 0) sqlite3_bind_double(s, 1, U[k].d); else if (U[k].kind == 1) sqlite3_bind_int(s, 1, U[k].i);
        else if (U[k].kind == 3) { if (U[k].i == -1) sqlite3_bind_null(s, 1); else sqlite3_bind_int(s, 1, U[k].i); }   /* -1 = no head (null) */
        else sqlite3_bind_text(s, 1, U[k].s, -1, SQLITE_STATIC);
        sqlite3_bind_blob(s, 2, U[k].who, 16, SQLITE_STATIC);
        int ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        if (!ok) { snprintf(err, errn, "%s", sqlite3_errmsg(db)); return 0; }
    }
    /* cutter material by name (TlAssemblyItem.TlToolMaterialID): a TlToolMaterial row of that name in the library, or
       copied from DatabaseDefaults.tooldb (ensure_ref); a name in neither leaves the reference as it is */
    if (t->has_extras) {
        static const char *mat[] = { "TlMaterial", "TlToolMaterial" };
        unsigned char mid[16]; int have = 0;
        for (int pass = 0; pass < 2 && !have; pass++) {
            char q[300]; snprintf(q, sizeof q, "select m.ID from %s.TlMaterial m join %s.TlToolMaterial tm on tm.ID = m.ID where m.Name = ? limit 1", pass ? "d" : "main", pass ? "d" : "main");
            sqlite3_stmt *s; if (sqlite3_prepare_v2(db, q, -1, &s, NULL) != SQLITE_OK) continue;
            sqlite3_bind_text(s, 1, t->mat_name, -1, SQLITE_STATIC);
            if (sqlite3_step(s) == SQLITE_ROW && sqlite3_column_bytes(s, 0) == 16) { memcpy(mid, sqlite3_column_blob(s, 0), 16); have = 1; }
            sqlite3_finalize(s);
        }
        if (have && !ensure_ref(db, mat, 2, mid, err, errn)) return 0;
        if (have) {
            sqlite3_stmt *s; if (sqlite3_prepare_v2(db, "update TlAssemblyItem set TlToolMaterialID = ? where ID = ?", -1, &s, NULL) == SQLITE_OK) {
                sqlite3_bind_blob(s, 1, mid, 16, SQLITE_STATIC); sqlite3_bind_blob(s, 2, id, 16, SQLITE_STATIC);
                int okm = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
                if (!okm) { snprintf(err, errn, "%s", sqlite3_errmsg(db)); return 0; }
            }
        }
    }
    /* OpToolInfo: Mastercam applies the blob, then the columns (2.4); patch the offsets 3.5 decodes so it is not stale */
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(db, "select OpToolInfo from TlTool where ID = ?", -1, &s, NULL) == SQLITE_OK) {
        sqlite3_bind_blob(s, 1, id, 16, SQLITE_STATIC);
        uint8_t b[648]; int have = 0;
        if (sqlite3_step(s) == SQLITE_ROW && sqlite3_column_bytes(s, 0) == 648) { memcpy(b, sqlite3_column_blob(s, 0), 648); have = 1; }
        sqlite3_finalize(s);
        if (have) {
            int32_t i32; int16_t i16; double d;
            i32 = t->num; memcpy(b + 0x00, &i32, 4); i16 = (int16_t)t->type; memcpy(b + 0x04, &i16, 2);
            i16 = (int16_t)t->rad_type; memcpy(b + 0x06, &i16, 2);
            d = t->dia; memcpy(b + 0x08, &d, 8); d = t->corner_r; memcpy(b + 0x10, &d, 8);
            d = t->is_metric ? t->pitch : (t->pitch > 0 ? 1.0 / t->pitch : 0); memcpy(b + 0x18, &d, 8);   /* inch: threads per inch */
            d = t->tip_angle; memcpy(b + 0x20, &d, 8);
            i32 = t->dia_off; memcpy(b + 0x28, &i32, 4); i32 = t->len_off; memcpy(b + 0x2c, &i32, 4);
            d = t->feed; memcpy(b + 0x30, &d, 8); d = t->plunge; memcpy(b + 0x38, &d, 8); d = t->retract; memcpy(b + 0x40, &d, 8);
            i32 = t->rpm; memcpy(b + 0x48, &i32, 4); i16 = (int16_t)tool_coolant_style(t); memcpy(b + 0x50, &i16, 2);
            i16 = (int16_t)t->flutes; memcpy(b + 0x52, &i16, 2);
            memset(b + 0x56, 0, 0x78); memcpy(b + 0x56, t->name, strlen(t->name) < 0x77 ? strlen(t->name) : 0x77);
            b[0x258] = t->is_metric ? 1 : 0;
            if (sqlite3_prepare_v2(db, "update TlTool set OpToolInfo = ? where ID = ?", -1, &s, NULL) == SQLITE_OK) {
                sqlite3_bind_blob(s, 1, b, 648, SQLITE_STATIC); sqlite3_bind_blob(s, 2, id, 16, SQLITE_STATIC);
                sqlite3_step(s); sqlite3_finalize(s);
            }
        }
    }
    return 1;
}
/* delete a tool (WritableDatabaseConnection, toollib_net.md 3.6): its coolant, op params, then the tool rows */
static int delete_tool(sqlite3 *db, const unsigned char *id, char *err, int errn) {
    unsigned char op[16], co[16]; int hop = get_guid(db, "select TlOpParamsID from TlToolMill where ID = ?", id, op);
    int hco = hop && get_guid(db, "select TlCoolantID from TlOpParams where ID = ?", op, co);
    const char *chain[] = { "TlToolSlotMill", "TlToolEndmill", "TlToolBoring", "TlToolCounterBore", "TlToolCenterDrill", "TlToolDrill",
                            "TlToolReamer", "TlToolThreading", "TlToolBarrelMill", "TlToolFeedMill", "TlToolMill", "TlTool", "TlAssemblyItem" };
    struct { const char *t; const unsigned char *id; int on; } D[] = { { "TlCoolant", co, hco }, { "TlMillingOpParams", op, hop },
        { "TlHolemakingOpParams", op, hop }, { "TlOpParams", op, hop } };
    for (size_t k = 0; k < sizeof D / sizeof D[0] + sizeof chain / sizeof *chain; k++) {
        const char *t = k < 4 ? D[k].t : chain[k - 4]; const unsigned char *who = k < 4 ? D[k].id : id;
        if (k < 4 && !D[k].on) continue;
        char q[200]; snprintf(q, sizeof q, "delete from %s where ID = ?", t);
        sqlite3_stmt *s; if (sqlite3_prepare_v2(db, q, -1, &s, NULL) != SQLITE_OK) { snprintf(err, errn, "%s", sqlite3_errmsg(db)); return 0; }
        sqlite3_bind_blob(s, 1, who, 16, SQLITE_STATIC); int ok = sqlite3_step(s) == SQLITE_DONE; sqlite3_finalize(s);
        if (!ok) { snprintf(err, errn, "%s", sqlite3_errmsg(db)); return 0; }
    }
    return 1;
}

/* Mastercam's own scripts for a new library (toollib_net.md 3.2): ToolManager.Database.dll (2017) embeds
   CreateToolDatabase.sql (SQL:create) and InsertBaseData.sql (SQL:base) as manifest resources, each stored as a u32
   length + the bytes (ECMA-335 II.24 .resources: no compression); found by their header comments, "/ * CreateDatabase.sql * /"
   and "/ * CreateDatabaseBaseData.sql * /", which occur once in the DLL. Looked for in $MCAMUI_TOOLDB_DLL, the paths.conf
   schema folders (where the 2017 DLLs are extracted) and C:\Program Files\mcam2017\Extensions. */
static char *tm_resource(const unsigned char *d, size_t n, const char *mark) {
    size_t ml = strlen(mark);
    for (size_t i = 0; i + ml <= n; i++) {
        if (memcmp(d + i, mark, ml)) continue;
        for (size_t b = i; b >= 4 && i - b < 512; b--)          /* back to the resource's UTF-8 BOM */
            if (b + 3 <= n && d[b] == 0xef && d[b + 1] == 0xbb && d[b + 2] == 0xbf) {
                uint32_t len; memcpy(&len, d + b - 4, 4);
                if (len < ml || len > n - b || b + len < i + ml) return NULL;
                char *t = xmalloc(len + 1); memcpy(t, d + b, len); t[len] = 0;
                return t;
            }
        return NULL;
    }
    return NULL;
}
static int tm_scripts(char **create, char **base, char *err, int errn) {
    char cand[12][1100]; int nc = 0; char one[1100];
    if (getenv("MCAMUI_TOOLDB_DLL")) snprintf(cand[nc++], 1100, "%s", getenv("MCAMUI_TOOLDB_DLL"));
    for (const char *r = site_path("schema"); nc < 10 && (r = path_list_next(r, one, sizeof one)); )
        if (one[0]) { char d[1100], q[1300]; host_path(one, d, sizeof d); path_join(q, sizeof q, d, "toolmanager.database.dll"); host_path(q, cand[nc++], 1100); }
    host_path("C:\\Program Files\\mcam2017\\Extensions\\ToolManager.Database.dll", cand[nc++], 1100);
    *create = *base = NULL;
    for (int k = 0; k < nc && !*create; k++) {
        FILE *f = fopen(cand[k], "rb"); if (!f) continue;
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        unsigned char *d = n > 0 ? xmalloc((size_t)n) : NULL;
        if (d && fread(d, 1, (size_t)n, f) == (size_t)n) {
            *create = tm_resource(d, (size_t)n, "/* CreateDatabase.sql */");
            *base = *create ? tm_resource(d, (size_t)n, "/* CreateDatabaseBaseData.sql */") : NULL;
            if (!*base) { free(*create); *create = NULL; }
        }
        free(d); fclose(f);
    }
    if (!*create) snprintf(err, errn, "Mastercam 2017's ToolManager.Database.dll not found (paths.conf schema folders, or MCAMUI_TOOLDB_DLL) - its scripts create a new tool library");
    return *create != NULL;
}
/* DatabaseCreator.CreateDatabaseFile (DatabaseCreator.cs L73-150): a new file; SQL:create in one transaction as one
   command; SQL:base in a second transaction line by line, skipping every line that contains "/ *" */
static int tm_create(const char *file, char *err, int errn) {
    char *cr, *ba; if (!tm_scripts(&cr, &ba, err, errn)) return 0;
    remove(file);
    sqlite3 *db; int ok = sqlite3_open(file, &db) == SQLITE_OK;
    char *e = NULL;
    const char *c0 = cr; if ((unsigned char)c0[0] == 0xef) c0 += 3;      /* the BOM is not SQL */
    if (ok) ok = sqlite3_exec(db, "begin", NULL, NULL, &e) == SQLITE_OK && sqlite3_exec(db, c0, NULL, NULL, &e) == SQLITE_OK
                 && sqlite3_exec(db, "commit", NULL, NULL, &e) == SQLITE_OK;
    if (ok) ok = sqlite3_exec(db, "begin", NULL, NULL, &e) == SQLITE_OK;
    for (char *l = ba, *nx; ok && l && *l; l = nx) {
        nx = strchr(l, '\n'); if (nx) *nx++ = 0;
        if (strstr(l, "/*")) continue;
        size_t k = strlen(l); while (k && (l[k - 1] == '\r' || l[k - 1] == ' ')) l[--k] = 0;
        if (!k) continue;
        ok = sqlite3_exec(db, l, NULL, NULL, &e) == SQLITE_OK;
    }
    if (ok) ok = sqlite3_exec(db, "commit", NULL, NULL, &e) == SQLITE_OK;
    if (!ok) snprintf(err, errn, "creating the library: %s", e ? e : sqlite3_errmsg(db));
    sqlite3_free(e);
    if (sqlite3_close(db) != SQLITE_OK) ok = 0;
    free(cr); free(ba);
    if (!ok) remove(file);
    return ok;
}

int toollib_save_tooldb(const char *path, ToolLib *L, char *err, int errn) {
    char defp[1100]; site_file(defp, sizeof defp, "shared", "common/ToolData/DatabaseDefaults.tooldb");
    if (!defp[0] || access(defp, R_OK) != 0) { snprintf(err, errn, "DatabaseDefaults.tooldb not found (paths.conf: shared) - needed for new rows"); return 0; }
    /* the base: the file itself, else (Save As) the library these tools were read from - holders, assemblies and other
       rows come along - else a new library made by Mastercam's scripts (tm_create) */
    const char *base = path; int fresh = 0;
    if (access(path, F_OK) != 0) {
        size_t lp = strlen(L->path);
        if (lp > 7 && !strcasecmp(L->path + lp - 7, ".tooldb") && access(L->path, R_OK) == 0) base = L->path;
        else fresh = 1;
    }
    char tmp[4200]; if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) { snprintf(err, errn, "path too long"); return 0; }
    if (fresh) { if (!tm_create(tmp, err, errn)) return 0; }
    else {   /* work on a copy: the library is replaced only by a complete one */
        FILE *in = fopen(base, "rb"), *out = in ? fopen(tmp, "wb") : NULL; char buf[65536]; size_t n; int ok = in && out;
        while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, n, out) != n) ok = 0;
        if (in) fclose(in); if (out && fclose(out) != 0) ok = 0;
        if (!ok) { remove(tmp); snprintf(err, errn, "cannot copy to %s", tmp); return 0; }
    }
    sqlite3 *db; if (sqlite3_open(tmp, &db) != SQLITE_OK) { snprintf(err, errn, "%s", sqlite3_errmsg(db)); sqlite3_close(db); remove(tmp); return 0; }
    char att[2300]; char *qd = sqlite3_mprintf("attach database %Q as d", defp); snprintf(att, sizeof att, "%s", qd); sqlite3_free(qd);
    int ok = db_exec(db, att, err, errn) && db_exec(db, "begin", err, errn);
    for (int k = 0; ok && k < L->ndel; k++) if (row_exists(db, "main", "TlTool", L->del[k])) ok = delete_tool(db, L->del[k], err, errn);
    for (int i = 0; ok && i < L->n; i++) {
        Tool *t = &L->t[i]; unsigned char op[16], co[16];
        if (t->has_guid && row_exists(db, "main", "TlTool", t->guid)) {        /* update in place */
            int hop = get_guid(db, "select TlOpParamsID from main.TlToolMill where ID = ?", t->guid, op);
            int hco = hop && get_guid(db, "select TlCoolantID from main.TlOpParams where ID = ?", op, co);
            ok = write_cols(db, t, t->guid, hop ? op : NULL, hco ? co : NULL, err, errn);
            continue;
        }
        /* new: GetDefaultTool(type, IsMetric) = the first defaults tool of that type and units, copied with new GUIDs */
        sqlite3_stmt *s; unsigned char did[16]; int found = 0;
        if (sqlite3_prepare_v2(db, "select a.ID from d.TlAssemblyItem a join d.TlToolMill m on m.ID = a.ID where m.MCToolType = ? and a.IsMetric = ? limit 1", -1, &s, NULL) == SQLITE_OK) {
            sqlite3_bind_int(s, 1, t->type); sqlite3_bind_int(s, 2, t->is_metric != 0);
            if (sqlite3_step(s) == SQLITE_ROW && sqlite3_column_bytes(s, 0) == 16) { memcpy(did, sqlite3_column_blob(s, 0), 16); found = 1; }
            sqlite3_finalize(s);
        }
        if (!found) { snprintf(err, errn, "no default tool of type %d (%s) in DatabaseDefaults.tooldb for %s", t->type, t->is_metric ? "mm" : "inch", t->name); ok = 0; break; }
        unsigned char nid[16], dop[16], dco[16], ref[16]; new_guid(nid); new_guid(op); new_guid(co);
        int hop = get_guid(db, "select TlOpParamsID from d.TlToolMill where ID = ?", did, dop);
        int hco = hop && get_guid(db, "select TlCoolantID from d.TlOpParams where ID = ?", dop, dco);
        static const char *mat[] = { "TlMaterial", "TlToolMaterial" }, *man[] = { "TlManufacturer" }, *grd[] = { "TlToolGrade" };
        if (get_guid(db, "select TlToolMaterialID from d.TlAssemblyItem where ID = ?", did, ref)) ok = ok && ensure_ref(db, mat, 2, ref, err, errn);
        if (get_guid(db, "select TlManufacturerID from d.TlAssemblyItem where ID = ?", did, ref)) ok = ok && ensure_ref(db, man, 1, ref, err, errn);
        if (get_guid(db, "select TlGradeID from d.TlToolMill where ID = ?", did, ref)) ok = ok && ensure_ref(db, grd, 1, ref, err, errn);
        if (ok && hco) ok = copy_row(db, "TlCoolant", dco, co, NULL, NULL, 0, err, errn);                       /* coolant first (non-deferred FK) */
        if (ok && hop) { const char *c[] = { "TlCoolantID" }; const unsigned char *v[] = { co };
            ok = copy_row(db, "TlOpParams", dop, op, c, v, hco, err, errn);
            if (ok && row_exists(db, "d", "TlMillingOpParams", dop)) ok = copy_row(db, "TlMillingOpParams", dop, op, NULL, NULL, 0, err, errn);
            if (ok && row_exists(db, "d", "TlHolemakingOpParams", dop)) ok = copy_row(db, "TlHolemakingOpParams", dop, op, NULL, NULL, 0, err, errn); }
        if (ok) ok = copy_row(db, "TlAssemblyItem", did, nid, NULL, NULL, 0, err, errn);
        if (ok) ok = copy_row(db, "TlTool", did, nid, NULL, NULL, 0, err, errn);
        if (ok) { const char *c[] = { "TlOpParamsID" }; const unsigned char *v[] = { op }; ok = copy_row(db, "TlToolMill", did, nid, c, v, hop, err, errn); }
        for (size_t k = 0; ok && k < sizeof TOOL_LEAVES / sizeof *TOOL_LEAVES; k++)
            if (row_exists(db, "d", TOOL_LEAVES[k], did)) ok = copy_row(db, TOOL_LEAVES[k], did, nid, NULL, NULL, 0, err, errn);
        if (ok) ok = db_exec(db, "", err, errn);
        if (ok) ok = write_cols(db, t, nid, hop ? op : NULL, hco ? co : NULL, err, errn);
        if (ok) { memcpy(t->guid, nid, 16); t->has_guid = 1; }
    }
    if (ok) ok = db_exec(db, "commit", err, errn); else sqlite3_exec(db, "rollback", NULL, NULL, NULL);
    sqlite3_exec(db, "detach database d", NULL, NULL, NULL);
    if (sqlite3_close(db) != SQLITE_OK) ok = 0;
    if (!ok) { remove(tmp); return 0; }
    if (file_replace(tmp, path) != 0) { remove(tmp); snprintf(err, errn, "cannot replace %s", path); return 0; }
    L->ndel = 0;
    if (path != L->path) snprintf(L->path, sizeof L->path, "%s", path);   /* (the same buffer when saving in place) */
    return 1;
}

int toollib_vacuum(const char *path, char *err, int errn) {
    sqlite3 *db;
    if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) { snprintf(err, errn, "%s", sqlite3_errmsg(db)); sqlite3_close(db); return 0; }
    int ok = db_exec(db, "VACUUM;", err, errn);
    if (sqlite3_close(db) != SQLITE_OK) ok = 0;
    return ok;
}

/* the wizard's Material list (toolmanager_ui.md 2.9: catalog tool materials sorted by name, InitializeDatabaseLists):
   names from common/ToolData/DatabaseCatalog.tooldb. Returns the count. */
int toollib_catalog_materials(char (*names)[32], int max) {
    char p[1100]; site_file(p, sizeof p, "shared", "common/ToolData/DatabaseCatalog.tooldb");
    sqlite3 *db; int n = 0;
    if (!p[0] || sqlite3_open_v2(p, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) { if (p[0]) sqlite3_close(db); return 0; }
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(db, "select m.Name from TlToolMaterial t join TlMaterial m on m.ID = t.ID order by m.Name", -1, &s, NULL) == SQLITE_OK) {
        while (n < max && sqlite3_step(s) == SQLITE_ROW) { const unsigned char *v = sqlite3_column_text(s, 0); snprintf(names[n++], 32, "%s", v ? (const char *)v : ""); }
        sqlite3_finalize(s);
    }
    sqlite3_close(db);
    return n;
}

/* Standard sizes of the wizard's drill / tap pages (SH DropdownGridView.LoadDropdownLists / GetDefaultTools,
   toolmanager_ui.md 2.6.y): catalog tools of the page's type (center drill 1, spot drill 2, drill / counterbore /
   countersink / bradpoint = that MCToolType; taps 4/5 = every TlToolThreading item), same IsMetric (null excluded),
   ordered by OverallDiameter (center drills by DrillDiameter). Values pass through double.ToString() (15 digits). */
static double g15(double v) { char b[40]; snprintf(b, sizeof b, "%.15g", v); return atof(b); }
int toollib_catalog_sizes(int type, int metric, StdSize **out) {
    *out = NULL;
    const char *q;
    if (type == 1) q = "select a.Name, m.ArborDiameter, cd.DrillDiameter, cd.DrillLength, m.OverallLength, 0 from TlToolCenterDrill cd"
                       " join TlToolDrill d on d.ID = cd.ID join TlToolMill m on m.ID = cd.ID join TlAssemblyItem a on a.ID = cd.ID"
                       " where m.MCToolType = 1 and a.IsMetric = ? order by cd.DrillDiameter";
    else if (type == 4 || type == 5) q = "select a.Name, m.OverallDiameter, m.CuttingDepth, m.OverallLength, th.ThreadPitch, 0 from TlToolThreading th"
                       " join TlToolMill m on m.ID = th.ID join TlAssemblyItem a on a.ID = th.ID where a.IsMetric = ? order by m.OverallDiameter";
    else if (type == 2 || type == 3 || type == 8 || type == 9 || type == 22)
        q = "select a.Name, m.OverallDiameter, m.CuttingDepth, m.OverallLength, 0, 0 from TlToolDrill d join TlToolMill m on m.ID = d.ID"
            " join TlAssemblyItem a on a.ID = d.ID where m.MCToolType = ?2 and a.IsMetric = ?1 order by m.OverallDiameter";
    else return 0;
    char p[1100]; site_file(p, sizeof p, "shared", "common/ToolData/DatabaseCatalog.tooldb");
    sqlite3 *db; int n = 0;
    if (!p[0] || sqlite3_open_v2(p, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) { if (p[0]) sqlite3_close(db); return 0; }
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(db, q, -1, &s, NULL) == SQLITE_OK) {
        sqlite3_bind_int(s, 1, metric != 0);
        if (sqlite3_bind_parameter_count(s) >= 2) sqlite3_bind_int(s, 2, type);
        while (sqlite3_step(s) == SQLITE_ROW) {
            *out = xrealloc(*out, sizeof(StdSize) * (n + 1));
            StdSize *r = &(*out)[n++]; memset(r, 0, sizeof *r);
            col_s(s, 0, r->name, sizeof r->name);
            double v[4]; for (int k = 0; k < 4; k++) v[k] = g15(col_d(s, k + 1));
            if (type == 1) { r->shank = v[0]; r->drill_dia = v[1]; r->drill_len = v[2]; r->oal = v[3]; }
            else { r->dia = v[0]; r->flute = v[1]; r->oal = v[2];
                   if (type == 4 || type == 5) r->thread_info = metric ? v[3] : (v[3] != 0 ? nearbyint(1e6 / v[3]) / 1e6 : 0); }   /* inch: Math.Round(1 / pitch, 6) */
        }
        sqlite3_finalize(s);
    }
    sqlite3_close(db);
    return n;
}
