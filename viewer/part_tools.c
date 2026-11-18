/*
 * part_tools.c - the part file's TlMgr archive (ASSOC_ID subtype 33), spec ../formats/part_tools.md:
 *   - an MFC CArchive stream (spec 2): class / object tags, map indices shared by classes and objects, one level tag
 *     per inheritance level (X::Serialize = Base::Serialize, SerializeClass(X), X's own fields)
 *   - root TlMgr::DeserializeV5 (mccore 0x18073dc60): n x { GUID key, ReadObject }, slot table, lathe slot table
 *   - X9 class versions (DeserializeVn readers) and the 2017 ones (Serialize writers), spec 3
 *   - a TL_ID tool is linked by slot (op_tool_info +0x264 = TL_ID ent +0x2cc) -> slot table -> TlAssembly ->
 *     MainToolID / MainHolderID (spec 1.1); its fields go to Tool per spec 4
 * Every read is bounds-checked; any unknown class, schema or overrun rejects the whole archive.
 * part_tools_write (at the end): the archive written back as TlMgr::Serialize stores it, with tools edited here (spec 10).
 */
#include "platform.h"
#include "part_tools.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LEGACY_N 0x288                      /* op_tool_info block in TlTool = TL_ID ent +0x68..+0x2f0 (spec 1.1) */
#define MAX_DEPTH 8                         /* nested ReadObject (tool -> op params, profile -> segment, ...) */

enum {                                      /* level classes (spec 2 table) */
    L_TlMgr, L_TlObject, L_TlAssemblyItem, L_TlTool, L_TlToolMill, L_TlToolEndmill, L_TlToolDrill, L_TlToolThreading,
    L_TlToolBoring, L_TlToolCenterDrill, L_TlToolCounterBore, L_TlToolSlotMill, L_TlToolReamer, L_TlToolFeedMill,
    L_TlToolBarrelMill, L_TlHolder, L_TlAssembly, L_TlProfile, L_TlProfileSegment, L_TlProfileLineSegment,
    L_TlProfileArcSegment, L_TlOpParams, L_TlMillingOpParams, L_TlHolemakingOpParams, L_TlCoolant, L_TlManufacturer,
    L_TlToolGrade, NLEV
};
static const char *const LEV[NLEV] = {
    "TlMgr", "TlObject", "TlAssemblyItem", "TlTool", "TlToolMill", "TlToolEndmill", "TlToolDrill", "TlToolThreading",
    "TlToolBoring", "TlToolCenterDrill", "TlToolCounterBore", "TlToolSlotMill", "TlToolReamer", "TlToolFeedMill",
    "TlToolBarrelMill", "TlHolder", "TlAssembly", "TlProfile", "TlProfileSegment", "TlProfileLineSegment",
    "TlProfileArcSegment", "TlOpParams", "TlMillingOpParams", "TlHolemakingOpParams", "TlCoolant", "TlManufacturer",
    "TlToolGrade"
};
enum { K_ASSY = 1, K_HOLDER, K_TOOL, K_SEG, K_OPP, K_EMB, K_OTHER };    /* object kinds */

/* leaf class -> inheritance levels, base first (spec 2, from each Serialize's base call) */
#define OB L_TlObject
#define TM L_TlObject, L_TlAssemblyItem, L_TlTool, L_TlToolMill
static const struct { int kind; signed char lv[7]; } CHAIN[] = {
    { K_ASSY,   { OB, L_TlAssembly, -1 } },
    { K_HOLDER, { OB, L_TlAssemblyItem, L_TlHolder, -1 } },
    { K_TOOL,   { TM, L_TlToolEndmill, -1 } },
    { K_TOOL,   { TM, L_TlToolDrill, -1 } },
    { K_TOOL,   { TM, L_TlToolThreading, -1 } },
    { K_TOOL,   { TM, L_TlToolDrill, L_TlToolBoring, -1 } },
    { K_TOOL,   { TM, L_TlToolDrill, L_TlToolCenterDrill, -1 } },
    { K_TOOL,   { TM, L_TlToolDrill, L_TlToolCounterBore, -1 } },
    { K_TOOL,   { TM, L_TlToolEndmill, L_TlToolSlotMill, -1 } },
    { K_TOOL,   { TM, L_TlToolReamer, -1 } },
    { K_TOOL,   { TM, L_TlToolFeedMill, -1 } },
    { K_TOOL,   { TM, L_TlToolBarrelMill, -1 } },
    { K_TOOL,   { TM, -1 } },                       /* a plain TlToolMill (types 0 / 20, CreateNewFromOldTool 0x180834f20) */
    { K_SEG,    { OB, L_TlProfileSegment, L_TlProfileLineSegment, -1 } },
    { K_SEG,    { OB, L_TlProfileSegment, L_TlProfileArcSegment, -1 } },
    { K_OPP,    { OB, L_TlOpParams, L_TlMillingOpParams, -1 } },
    { K_OPP,    { OB, L_TlOpParams, L_TlHolemakingOpParams, -1 } },
    /* the 2017 writer's optional objects (tlcore Serialize: TlObject::Serialize, then the class level):
       TlAssemblyItem v9 +0xf0 TlManufacturer (0x18000a0b0), TlToolMill v12 +0x8a8 TlToolGrade (0x18000fc6d) */
    { K_OTHER,  { OB, L_TlManufacturer, -1 } },
    { K_OTHER,  { OB, L_TlToolGrade, -1 } },
    { K_EMB,    { OB, L_TlProfile, -1 } },              /* embedded members: Serialize only, no object index */
    { K_EMB,    { OB, L_TlCoolant, -1 } },
};
#undef OB
#undef TM
enum { CH_PROFILE = 19, CH_COOLANT = 20 };
static int chain_of(int leaf) {                 /* the chain whose last level is LEAF (embedded ones excluded) */
    for (int i = 0; i < CH_PROFILE; i++) {
        int k = 0; while (CHAIN[i].lv[k + 1] >= 0) k++;
        if (CHAIN[i].lv[k] == leaf) return i;
    }
    return -1;
}

typedef struct Obj {
    int kind, leaf;
    uint8_t id[16]; char name[128], desc[128]; int slot;            /* TlObject (3.1); slot -1 = none */
    int is_metric;                                                  /* TlAssemblyItem +0x84 (3.2) */
    double *prof; int nprof, prof_arc;                              /* stored TlProfile (3.7): nprof lines (x0, y0, x1, y1) */
    int is_line; double seg[5];                                     /* TlProfileLine / ArcSegment (3.7) */
    const uint8_t *legacy; int num, station; char mfg[128];         /* TlTool (3.3) */
    struct Obj *op;                                                 /* TlToolMill +0x888 op params (3.4) */
    double oal, sl, fl, dia, da, dsh, tl, dn, true_len, req_pilot, helix_angle;
    int shank, spindle, flutes, helix_type, mc_type; char chuck[128];
    double corner_r, taper_angle, tip_dia, chamfer_len, tip_angle, shoulder_angle, pitch, root_dia, thread_angle;   /* 3.5 */
    int radius_type, nstarts, thread_shape, sec_radius_type;
    double sec_corner_r, sec_chamfer, sec_chamfer_angle, drill_dia, drill_len, pilot_dia, pilot_len, shift;
    double lower_r, upper_r, lower_taper, upper_taper, relief_taper, barrel_len, profile_r;
    uint8_t tool_id[16], holder_id[16]; double stickout;             /* TlAssembly (3.9) */
    double sfm_adj, fpt_adj; int is_rough, is_finish, cycle;        /* op params (3.8) */
    double step[4], peck1, peck2, peck_clear, chipbreak, dwell;
} Obj;

typedef struct {
    const uint8_t *b; size_t p, n; int err, depth;
    struct { int lev, schema; Obj *obj; struct WO *wo; } *m; int nm, cap;   /* map: index 1.. = class (lev >= 0) or object
                                                                       (obj: part_tools_apply / list; wo: part_tools_write) */
    Obj **all; int nall;                                            /* every object read (to free) */
    Obj **item; int nitem;                                          /* the TlMgr list */
    struct { int slot; uint8_t id[16]; } *st; int nst;              /* slot table (TlMgr +0x30) */
} Ar;

/* ---- primitives (bounds-checked: an overrun sets err and returns 0) ---- */
static int need(Ar *a, size_t k) { if (a->err || k > a->n - a->p) { a->err = 1; return 0; } return 1; }
static unsigned u8(Ar *a) { return need(a, 1) ? a->b[a->p++] : 0; }
static unsigned u16(Ar *a) { if (!need(a, 2)) return 0; unsigned v = a->b[a->p] | a->b[a->p + 1] << 8; a->p += 2; return v; }
static uint32_t u32(Ar *a) { if (!need(a, 4)) return 0; const uint8_t *q = a->b + a->p; a->p += 4; return q[0] | q[1] << 8 | q[2] << 16 | (uint32_t)q[3] << 24; }
static int32_t i32(Ar *a) { return (int32_t)u32(a); }
static double f64(Ar *a) { double d = 0; if (need(a, 8)) { memcpy(&d, a->b + a->p, 8); a->p += 8; } return d; }
static void raw(Ar *a, void *dst, size_t k) { if (need(a, k)) { if (dst) memcpy(dst, a->b + a->p, k); a->p += k; } }
static int count(Ar *a, size_t unit) {          /* i32 element count; < 0 reads none (the readers' loop) */
    int32_t k = i32(a);
    if (k < 0) return 0;
    if ((size_t)k > (a->n - a->p) / unit) { a->err = 1; return 0; }
    return k;
}
static void cstr(Ar *a, char *dst, size_t cap) {    /* CString: u8 len (0xFF -> u16; 0xFFFF -> u32); 0xFFFE unicode */
    if (dst) dst[0] = 0;
    uint32_t len = u8(a);
    if (len == 0xff) { len = u16(a); if (len == 0xfffe) { a->err = 1; return; } if (len == 0xffff) len = u32(a); }
    if (!need(a, len)) return;
    if (dst) {                                  /* as mcam_load cstr: up to a NUL, non-printable -> '?' */
        size_t i = 0; const uint8_t *s = a->b + a->p;
        for (; i + 1 < cap && i < len && s[i]; i++) dst[i] = (s[i] >= 32 && s[i] < 127) ? s[i] : '?';
        dst[i] = 0;
    }
    a->p += len;
}
static void guid_list(Ar *a) { int k = count(a, 16); raw(a, NULL, (size_t)k * 16); }

/* ---- CArchive tags (spec 2) ---- */
static int add_map(Ar *a, int lev, int schema, Obj *o) {
    if (a->nm == a->cap) { a->cap = a->cap ? a->cap * 2 : 64; a->m = xrealloc(a->m, sizeof *a->m * a->cap); }
    a->m[a->nm].lev = lev; a->m[a->nm].schema = schema; a->m[a->nm].obj = o; a->m[a->nm].wo = NULL;
    return a->nm++;
}
static uint32_t tag(Ar *a, uint32_t *clsbit) {      /* u16; 0x7FFF -> u32 big tag */
    uint32_t t = u16(a); *clsbit = 0x8000;
    if (t == 0x7fff) { t = u32(a); *clsbit = 0x80000000u; }
    return t;
}
static int new_class(Ar *a) {                       /* after FFFF: u16 schema, u16 len, name -> map index, -1 */
    unsigned schema = u16(a), len = u16(a);
    char nm[32];
    if (len >= sizeof nm || !need(a, len)) { a->err = 1; return -1; }
    memcpy(nm, a->b + a->p, len); nm[len] = 0; a->p += len;
    for (int l = 0; l < NLEV; l++) if (!strcmp(nm, LEV[l])) return add_map(a, l, (int)schema, NULL);
    a->err = 1; return -1;                          /* a class this reader does not know */
}
static int class_ref(Ar *a, uint32_t t, uint32_t clsbit) {   /* 0x8000|i (big: bit 31) = existing class i */
    uint32_t i = t & (clsbit - 1);
    if (!(t & clsbit) || i == 0 || i >= (uint32_t)a->nm || a->m[i].lev < 0) { a->err = 1; return -1; }
    return (int)i;
}
static int level(Ar *a, int want) {                 /* ReadClass for a level tag: its schema, -1 on error */
    uint32_t bit, t = tag(a, &bit);
    if (a->err) return -1;
    int i = t == 0xffff ? new_class(a) : class_ref(a, t, bit);
    if (a->err || a->m[i].lev != want) { a->err = 1; return -1; }
    return a->m[i].schema;
}

static void serialize(Ar *a, Obj *o, int ch);
static Obj *read_object(Ar *a) {                    /* ReadObject: NULL (null tag or error: see a->err), back-ref, new */
    uint32_t bit, t = tag(a, &bit);
    if (a->err || t == 0) return NULL;
    int i;
    if (t == 0xffff) i = new_class(a);
    else if (t & bit) i = class_ref(a, t, bit);
    else {                                          /* back-reference to object t */
        if (t >= (uint32_t)a->nm || !a->m[t].obj) { a->err = 1; return NULL; }
        return a->m[t].obj;
    }
    if (a->err) return NULL;
    int ch = chain_of(a->m[i].lev);
    if (ch < 0 || a->depth >= MAX_DEPTH) { a->err = 1; return NULL; }
    Obj *o = xcalloc(1, sizeof *o);
    o->kind = CHAIN[ch].kind; o->leaf = a->m[i].lev; o->slot = -1;
    a->all = xrealloc(a->all, sizeof *a->all * (a->nall + 1)); a->all[a->nall++] = o;
    add_map(a, -1, 0, o);
    a->depth++; serialize(a, o, ch); a->depth--;
    return o;
}

/* embedded TlProfile (GetStoredProfile -> Serialize -> SetStoredProfile): levels only, into a scratch object */
static void stored_profile(Ar *a, Obj *o) {
    Obj p; memset(&p, 0, sizeof p); p.slot = -1;
    if (a->depth >= MAX_DEPTH) { a->err = 1; return; }
    a->depth++; serialize(a, &p, CH_PROFILE); a->depth--;
    free(o->prof); o->prof = p.prof; o->nprof = p.nprof; o->prof_arc = p.prof_arc;
}

/* ---- levels (spec 3); every schema not seen in the files (or written by the 2017 writer) is rejected ---- */
static void lev(Ar *a, Obj *o, int l, int v) {
    switch (l) {
    case L_TlObject:            /* 3.1 v1 tlcore TlObject::DeserializeV1 0x180005ad3; v2 writer 0x18000cf9f */
        if (v != 1 && v != 2) break;
        raw(a, o->id, 16); cstr(a, o->name, sizeof o->name); cstr(a, o->desc, sizeof o->desc);
        if (v == 2) o->slot = i32(a);                                       /* GetSlot */
        return;
    case L_TlAssemblyItem:      /* 3.2 v7 DeserializeV7 0x1800027f2 (-> V2); v9 writer TlAssemblyItem::Serialize 0x18000d8aa */
        if (v != 7 && v != 9) break;
        for (int k = 0; k < 4; k++) cstr(a, NULL, 0);                     /* catalog id, location, image, geometry file */
        i32(a); o->is_metric = u8(a);                                       /* +0x80 quantity, +0x84 IsMetric */
        raw(a, NULL, v == 7 ? 64 : 32);                                     /* GUIDs +0x88, (+0x118,) +0xa0, (manufacturer) */
        guid_list(a);                                                       /* +0x100 */
        u8(a); raw(a, NULL, 16); u8(a); f64(a); f64(a);                     /* +0x85, +0xb8, +0x86, gauge dia / length */
        { int32_t k = i32(a); if (k > 0) raw(a, NULL, (size_t)k); }         /* +0x68 model data */
        if (v == 9) { stored_profile(a, o); if (u8(a)) read_object(a); }   /* +0xe0 stored profile, +0xf0 manufacturer */
        return;
    case L_TlTool:              /* 3.3 v13 TlTool::DeserializeV13 0x1807511b0; v15 writer 0x1807547b0 */
        if (v != 13 && v != 15) break;
        if (v == 13) stored_profile(a, o);
        if (need(a, LEGACY_N)) o->legacy = a->b + a->p;
        raw(a, NULL, LEGACY_N);
        if (v == 13) o->slot = i32(a);                                      /* TlObject::SetSlot */
        o->num = i32(a); o->station = i32(a); i32(a);                       /* +0x130 ToolNumber, +0x134 ToolStation, +0x138 */
        cstr(a, o->mfg, sizeof o->mfg); u8(a); i32(a);                      /* +0x140 MfgToolCode, +0x148 IsCustom, +0x150 */
        if (v == 15) i32(a);                                                /* +0x14c GeometryLevel */
        return;
    case L_TlToolMill:          /* 3.4 v10 TlToolMill::DeserializeV10 0x1808351b0; v12 writer 0x1808410f0 */
        if (v != 10 && v != 12) break;
        o->op = read_object(a);                                             /* +0x888 */
        if (o->op && o->op->kind != K_OPP) { a->err = 1; return; }
        f64(a); f64(a);                                                     /* +0x878 / +0x880 legacy holder length / dia */
        o->oal = f64(a); o->sl = f64(a); o->fl = f64(a); o->dia = f64(a);   /* +0x7f0 +0x7f8 +0x800 +0x808 */
        o->da = f64(a); o->dsh = f64(a);                                    /* +0x810 +0x818 */
        o->shank = i32(a); o->tl = f64(a); o->dn = f64(a); o->spindle = i32(a);     /* +0x820 +0x828 +0x830 +0x840 */
        u8(a); o->true_len = f64(a); o->req_pilot = f64(a);                 /* +0x86c, +0x870 TruePhysicalLength, +0x838 */
        if (v == 10) { cstr(a, NULL, 0); i32(a); }                          /* SetGeometryFile, +0x14c GeometryLevel */
        u8(a);                                                              /* +0x86b IsScalable */
        o->mc_type = i32(a); i32(a); i32(a);                                /* +0x8b8 MCToolType, +0x850, +0x854 */
        o->flutes = (int16_t)u16(a); o->helix_type = i32(a); o->helix_angle = f64(a);   /* +0x858 +0x85c +0x860 */
        cstr(a, o->chuck, sizeof o->chuck);                                 /* +0x848 */
        raw(a, NULL, 3);                                                    /* +0x86a +0x868 +0x869 */
        raw(a, NULL, v == 10 ? 0x10 + 0x78 : 0x78);                         /* (discarded 16,) +0x8f0 */
        guid_list(a);                                                       /* +0x8c0 */
        if (v == 12 && u8(a)) read_object(a);                               /* +0x8a8 tool grade */
        return;
    case L_TlToolEndmill:       /* 3.5 DeserializeV5 0x180766b00 */
        if (v != 5) break;
        o->corner_r = f64(a); o->taper_angle = f64(a); o->tip_dia = f64(a);    /* +0x988 +0x968 +0x970 */
        o->radius_type = i32(a); o->chamfer_len = f64(a);                       /* +0x978 +0x980 */
        return;
    case L_TlToolDrill:         /* 3.5 DeserializeV3 0x18075e3a0 */
        if (v != 3) break;
        o->corner_r = f64(a); o->tip_angle = f64(a); o->shoulder_angle = f64(a);    /* +0x978 +0x968 +0x970 */
        return;
    case L_TlToolThreading:     /* 3.5 DeserializeV7 0x18085dfc0 */
        if (v != 7) break;
        o->tip_angle = f64(a); o->corner_r = f64(a); o->pitch = f64(a); o->radius_type = i32(a);   /* +0x968 +0x9a0 +0x970 +0x990 */
        o->root_dia = f64(a); o->thread_angle = f64(a); o->tip_dia = f64(a);                       /* +0x978 +0x980 +0x988 */
        o->nstarts = i32(a); o->thread_shape = i32(a);                                             /* +0x994 +0x998 */
        return;
    /* 3.5 no sample in any file: 2017 writer order, UNVERIFIED on data */
    case L_TlToolBoring:        /* writer 0x18075a850 */
        if (v != 2) break;
        o->taper_angle = f64(a); o->radius_type = i32(a); o->shift = f64(a);   /* +0x980 +0x990 +0x988 */
        return;
    case L_TlToolCenterDrill:   /* writer 0x180a2ff50 */
        if (v != 1) break;
        o->drill_dia = f64(a); o->drill_len = f64(a); return;                  /* +0x980 +0x988 */
    case L_TlToolCounterBore:   /* writer 0x180a31870 */
        if (v != 1) break;
        o->pilot_dia = f64(a); o->pilot_len = f64(a); return;                  /* +0x980 +0x988 */
    case L_TlToolSlotMill:      /* writer 0x180a34000 */
        if (v != 2) break;
        o->sec_radius_type = i32(a); o->sec_corner_r = f64(a); o->sec_chamfer = f64(a); o->sec_chamfer_angle = f64(a);
        return;                                                                 /* +0x990 +0x998 +0x9a0 +0x9a8 */
    case L_TlToolReamer:        /* writer 0x180855250 */
        if (v != 5) break;
        o->tip_angle = f64(a); o->chamfer_len = f64(a); return;                /* +0x968 +0x970 */
    case L_TlToolFeedMill:      /* writer 0x180a37a40 */
        if (v != 1) break;
        o->tip_dia = f64(a); o->lower_r = f64(a); o->upper_r = f64(a);          /* +0x968 +0x970 +0x978 */
        o->lower_taper = f64(a); o->upper_taper = f64(a); o->relief_taper = f64(a);   /* +0x990 +0x980 +0x988 */
        return;
    case L_TlToolBarrelMill:    /* writer 0x180a3a3f0 */
        if (v != 1) break;
        o->barrel_len = f64(a); o->profile_r = f64(a); o->corner_r = f64(a); return;   /* +0x968 +0x970 +0x978 */
    case L_TlHolder:            /* 3.6 v4 TlHolder::DeserializeV4 0x1807281f0; v5 writer 0x18072add0 */
        if (v != 4 && v != 5) break;
        if (v == 4) stored_profile(a, o);                                   /* the holder outline */
        cstr(a, NULL, 0); i32(a); cstr(a, NULL, 0); cstr(a, NULL, 0);       /* +0x130 library, +0x138, +0x140, +0x150 */
        i32(a); i32(a);                                                     /* +0x148 / +0x158 connection types */
        if (v == 4) { raw(a, NULL, 8); cstr(a, NULL, 0); }                  /* discarded 8 bytes, SetGeometryFile */
        return;
    case L_TlAssembly:          /* 3.9 v8 TlAssembly::DeserializeV8 0x180714060; v9 writer 0x1807179d0 */
        if (v != 8 && v != 9) break;
        u32(a); raw(a, o->holder_id, 16); raw(a, o->tool_id, 16);           /* +0x48, +0x78 MainHolderID, +0x60 MainToolID */
        u32(a); u32(a); u32(a); o->stickout = f64(a); u8(a);                /* +0x4c +0x50 +0x54, +0x90 Stickout, +0x58 */
        if (v == 8) o->slot = i32(a);
        f64(a);                                                             /* +0xa0 MaxRamp */
        return;
    case L_TlProfile: {         /* 3.7 tlcore DeserializeV2 0x1800027d9: u32 n, n x ReadObject(TlProfileSegment) */
        if (v != 2) break;
        uint32_t k = u32(a);
        if (k > (a->n - a->p) / 2) { a->err = 1; return; }
        for (uint32_t i = 0; i < k && !a->err; i++) {
            Obj *s = read_object(a);
            if (a->err) return;
            if (!s || s->kind != K_SEG) { a->err = 1; return; }
            if (!s->is_line) {      /* an arc: a_2d {cx +0x58, cy +0x60, r +0x68, start +0x70, sweep +0x78} (GetArc), as 100
                                       lines, as tlcore TlProfileArcSegment::LinearizeArc 0x180005461 does it */
                double cx = s->seg[3], cy = s->seg[4], r = s->seg[2], a0 = s->seg[0], sw = s->seg[1];
                o->prof = xrealloc(o->prof, sizeof(double) * 4 * (o->nprof + 100));
                for (int k = 0; k < 100; k++) {
                    double *q = o->prof + 4 * o->nprof++;
                    q[0] = cos(a0 + k * sw / 100.0) * r + cx; q[1] = sin(a0 + k * sw / 100.0) * r + cy;
                    q[2] = cos(a0 + (k + 1) * sw / 100.0) * r + cx; q[3] = sin(a0 + (k + 1) * sw / 100.0) * r + cy;
                }
                continue;
            }
            o->prof = xrealloc(o->prof, sizeof(double) * 4 * (o->nprof + 1));
            memcpy(o->prof + 4 * o->nprof++, s->seg, sizeof(double) * 4);
        }
        return;
    }
    case L_TlProfileSegment:    /* 3.7 0x180003f21: u8 +0x48 Color */
        if (v != 1) break;
        u8(a); return;
    case L_TlProfileLineSegment:    /* 3.7 0x18000c351: (x0 = radius, y0 = height, x1, y1) */
        if (v != 1) break;
        for (int k = 0; k < 4; k++) o->seg[k] = f64(a);
        o->is_line = 1; return;
    case L_TlProfileArcSegment:     /* 3.7 0x1800011d6: +0x70 start, +0x78 sweep, +0x68 r, +0x58 / +0x60 centre (GetArc a_2d) */
        if (v != 1) break;
        for (int k = 0; k < 5; k++) o->seg[k] = f64(a);
        return;
    case L_TlOpParams: {        /* 3.8 0x18000d797: embedded TlCoolant, +0x48 +0x50 +0x58 +0x68 +0x60, long +0x70 */
        if (v != 1) break;
        Obj c; memset(&c, 0, sizeof c);
        if (a->depth >= MAX_DEPTH) { a->err = 1; return; }
        a->depth++; serialize(a, &c, CH_COOLANT); a->depth--;
        o->sfm_adj = f64(a); o->fpt_adj = f64(a); f64(a); f64(a); f64(a); i32(a);   /* feeds / rpm: = TL_ID (spec 7 check 2) */
        return;
    }
    case L_TlManufacturer:      /* tlcore DeserializeV1 0x180008b4d: no fields of its own */
        if (v != 1) break;
        return;
    case L_TlToolGrade:         /* tlcore DeserializeV1 0x180006244 / Serialize 0x18000fc6d: GUID +0x48 */
        if (v != 1) break;
        raw(a, NULL, 16); return;
    case L_TlCoolant:           /* 3.8 0x18000a380: 21 x i32 (meaning NOT DECODED) */
        if (v != 1) break;
        raw(a, NULL, 21 * 4); return;
    case L_TlMillingOpParams:   /* 3.8 0x18000d5ad */
        if (v != 1) break;
        o->is_rough = u8(a); o->is_finish = u8(a);
        for (int k = 0; k < 4; k++) o->step[k] = f64(a);
        return;
    case L_TlHolemakingOpParams:    /* 3.8 0x18000d25b */
        if (v != 1) break;
        o->cycle = i32(a); o->peck1 = f64(a); o->peck2 = f64(a); o->peck_clear = f64(a); o->chipbreak = f64(a); o->dwell = f64(a);
        return;
    }
    a->err = 1;                 /* unknown schema */
}

static void serialize(Ar *a, Obj *o, int ch) {
    for (int k = 0; CHAIN[ch].lv[k] >= 0 && !a->err; k++) {
        int v = level(a, CHAIN[ch].lv[k]);
        if (a->err) return;
        lev(a, o, CHAIN[ch].lv[k], v);
    }
}

static void ar_free(Ar *a) {
    for (int i = 0; i < a->nall; i++) { free(a->all[i]->prof); free(a->all[i]); }
    free(a->all); free(a->m); free(a->item); free(a->st);
}

/* spec 1 / 6: find FF FF 05 00 05 00 "TlMgr", read TlMgr::DeserializeV5; the stream must be consumed exactly */
static int parse(Ar *a, const uint8_t *rec, size_t n) {
    static const uint8_t TAG[11] = { 0xff, 0xff, 5, 0, 5, 0, 'T', 'l', 'M', 'g', 'r' };
    memset(a, 0, sizeof *a);
    size_t k = 0;
    while (k + sizeof TAG <= n && memcmp(rec + k, TAG, sizeof TAG)) k++;
    if (k + sizeof TAG > n) return -1;
    a->b = rec + k; a->n = n - k;
    add_map(a, -1, 0, NULL);                                    /* map indices start at 1 */
    if (level(a, L_TlMgr) != 5) return -1;
    int ni = count(a, 18);                                      /* GUID key + object tag */
    a->item = xmalloc(sizeof *a->item * (ni ? ni : 1));
    for (int i = 0; i < ni && !a->err; i++) { raw(a, NULL, 16); a->item[a->nitem++] = read_object(a); }
    int ns = count(a, 20);
    a->st = xmalloc(sizeof *a->st * (ns ? ns : 1));
    for (int i = 0; i < ns && !a->err; i++) { a->st[i].slot = i32(a); raw(a, a->st[i].id, 16); a->nst = i + 1; }
    int nl = count(a, 20);                                      /* lathe slot table (empty in every file) */
    raw(a, NULL, (size_t)nl * 20);
    return a->err || a->p != a->n ? -1 : 0;
}

static const Obj *find_item(const Ar *a, const uint8_t *id, int kind) {     /* by TlObject ID; the last one wins */
    for (int i = a->nitem - 1; i >= 0; i--)
        if (a->item[i] && !memcmp(a->item[i]->id, id, 16)) return a->item[i]->kind == kind ? a->item[i] : NULL;
    return NULL;
}

/* spec 1.1: slot -> slot table -> TlAssembly -> MainToolID / MainHolderID; the assembly's and the tool's slot equal it */
static int resolve(const Ar *a, int slot, const Obj **T, const Obj **H, const Obj **A) {
    for (int i = 0; i < a->nst; i++) {
        if (a->st[i].slot != slot) continue;
        *A = find_item(a, a->st[i].id, K_ASSY);
        if (!*A || (*A)->slot != slot) return 0;
        *T = find_item(a, (*A)->tool_id, K_TOOL);
        if (!*T || (*T)->slot != slot || !(*T)->legacy) return 0;
        *H = find_item(a, (*A)->holder_id, K_HOLDER);       /* NULL = no holder */
        return 1;
    }
    return 0;
}

static void scpy(char *dst, size_t cap, const char *src) { size_t k = strlen(src); if (k >= cap) k = cap - 1; memcpy(dst, src, k); dst[k] = 0; }

/* spec 4 field map */
static void fill(Tool *t, const Obj *T, const Obj *H, const Obj *A) {
    scpy(t->desc, sizeof t->desc, T->desc);                   /* TlObject +0x20 */
    t->is_metric = T->is_metric;                                        /* TlAssemblyItem +0x84 */
    scpy(t->mfg_code, sizeof t->mfg_code, T->mfg);            /* TlTool +0x140 */
    t->head_num = T->station;                                           /* TlTool +0x134 */
    t->oal = T->oal; t->shoulder_len = T->sl; t->flute_len = T->fl;     /* TlToolMill (3.4) */
    t->arbor_dia = T->da; t->shoulder_dia = T->dsh; t->shank_type = T->shank; t->taper_len = T->tl; t->neck_dia = T->dn;
    t->flutes = T->flutes; t->helix_type = T->helix_type; t->helix_angle = T->helix_angle;
    t->spindle_dir = T->spindle; t->req_pilot_dia = T->req_pilot;
    scpy(t->chuck, sizeof t->chuck, T->chuck);
    t->true_len = T->true_len;                                          /* +0x870 (spec 5) */
    switch (T->leaf) {                                                  /* 3.5 / spec 4 */
    case L_TlToolSlotMill:
        t->sec_radius_type = T->sec_radius_type; t->sec_corner_r = T->sec_corner_r;
        t->sec_chamfer = T->sec_chamfer; t->sec_chamfer_angle = T->sec_chamfer_angle;
        /* fall through - base TlToolEndmill */
    case L_TlToolEndmill:
        t->corner_r = T->corner_r; t->taper_angle = T->taper_angle; t->tip_dia = T->tip_dia;
        t->radius_type = T->radius_type; t->chamfer_len = T->chamfer_len;
        t->tip_angle = T->taper_angle;                                  /* = op_tool_info +0x20 (spec 4: 55/55 endmills) */
        break;
    case L_TlToolDrill: case L_TlToolBoring: case L_TlToolCenterDrill: case L_TlToolCounterBore:
        t->corner_r = T->corner_r; t->tip_angle = T->tip_angle; t->shoulder_angle = T->shoulder_angle;
        if (T->leaf == L_TlToolBoring) {
            t->taper_angle = T->taper_angle; t->radius_type = T->radius_type; t->shift = T->shift;
            t->tip_angle = T->taper_angle;
        }
        if (T->leaf == L_TlToolCenterDrill) { t->drill_dia = T->drill_dia; t->drill_len = T->drill_len; }
        if (T->leaf == L_TlToolCounterBore) { t->pilot_dia = T->pilot_dia; t->pilot_len = T->pilot_len; }
        break;
    case L_TlToolThreading:
        t->tip_angle = T->tip_angle; t->corner_r = T->corner_r; t->pitch = T->pitch; t->radius_type = T->radius_type;
        t->root_dia = T->root_dia; t->thread_angle = T->thread_angle; t->tip_dia = T->tip_dia;
        t->nstarts = T->nstarts; t->thread_shape = T->thread_shape;
        break;
    case L_TlToolReamer:
        t->tip_angle = T->tip_angle; t->chamfer_len = T->chamfer_len; break;
    case L_TlToolFeedMill:
        t->tip_dia = T->tip_dia; t->lower_r = T->lower_r; t->upper_r = T->upper_r;
        t->lower_taper = T->lower_taper; t->upper_taper = T->upper_taper; t->relief_taper = T->relief_taper; break;
    case L_TlToolBarrelMill:
        t->barrel_len = T->barrel_len; t->profile_r = T->profile_r; t->corner_r = T->corner_r; break;
    }
    if (T->op) {                                                        /* 3.8 */
        t->sfm_adj = T->op->sfm_adj; t->fpt_adj = T->op->fpt_adj;
        if (T->op->leaf == L_TlMillingOpParams) {
            t->is_rough = T->op->is_rough; t->is_finish = T->op->is_finish;
            t->step_rxy = T->op->step[0]; t->step_rz = T->op->step[1]; t->step_fxy = T->op->step[2]; t->step_fz = T->op->step[3];
        } else {
            t->cycle = T->op->cycle; t->peck1 = T->op->peck1; t->peck2 = T->op->peck2;
            t->peck_clear = T->op->peck_clear; t->chipbreak = T->op->chipbreak; t->dwell = T->op->dwell;
        }
    }
    t->stickout = A->stickout;                                          /* TlAssembly +0x90 (spec 5) */
    scpy(t->assembly, sizeof t->assembly, A->name);
    free(t->holder_rz); t->holder_rz = NULL; t->holder_n = 0; t->holder[0] = 0;
    if (H) {
        scpy(t->holder, sizeof t->holder, H->name);           /* holder TlObject Name */
        if (H->nprof) {                                                 /* (x0, y0) per line, plus the last (x1, y1) */
            t->holder_n = H->nprof + 1;
            t->holder_rz = xmalloc(sizeof(double) * 2 * t->holder_n);
            for (int i = 0; i < H->nprof; i++) { t->holder_rz[2 * i] = H->prof[4 * i]; t->holder_rz[2 * i + 1] = H->prof[4 * i + 1]; }
            t->holder_rz[2 * H->nprof] = H->prof[4 * H->nprof - 2]; t->holder_rz[2 * H->nprof + 1] = H->prof[4 * H->nprof - 1];
        }
    }
    t->has_extras = 1;
}

int part_tools_apply(Tool *tools, int ntools, const uint8_t *const *tl_body, const uint8_t *rec, size_t n) {
    Ar a; int filled = 0;
    if (parse(&a, rec, n)) { ar_free(&a); return -1; }
    for (int i = 0; i < ntools; i++) {
        const Obj *T, *H, *A;
        if (tools[i].slot < 0 || !tl_body[i] || !resolve(&a, tools[i].slot, &T, &H, &A)) continue;
        if (memcmp(T->legacy, tl_body[i], LEGACY_N)) continue;          /* spec 1.1: the TlTool block = the TL_ID body */
        fill(&tools[i], T, H, A); filled++;
    }
    ar_free(&a);
    return filled;
}

int part_tools_list(const uint8_t *rec, size_t n, Tool **out) {
    Ar a; int k = 0;
    *out = NULL;
    if (parse(&a, rec, n)) { ar_free(&a); return -1; }
    *out = xcalloc(a.nst ? a.nst : 1, sizeof **out);
    for (int i = 0; i < a.nst; i++) {
        const Obj *T, *H, *A;
        if (!resolve(&a, a.st[i].slot, &T, &H, &A)) continue;
        Tool *t = &(*out)[k++];
        t->slot = a.st[i].slot; t->num = T->num; t->dia = T->dia; t->type = T->mc_type;   /* +0x130, +0x808, +0x8b8 */
        if (T->legacy) { memcpy(t->op_info, T->legacy, LEGACY_N); t->has_op_info = 1; }   /* spec 1.1: the TlTool raw block */
        scpy(t->name, sizeof t->name, T->name);
        fill(t, T, H, A);
    }
    ar_free(&a);
    return k;
}

/* ================================================================================================ the writer
 * part_tools_write (spec 10): the archive as Mastercam's store branch writes it.
 *  - TlMgr::Serialize store branch (mccore 0x180744620, decomp/mccore_tlserialize.c l.9445-9588): SerializeClass(TlMgr),
 *    i32 n + n x { GUID key, WriteObject } over the std::map<TlID, TlObject> that PrepareObjectsForStoring
 *    (0x180743640, l.8393) builds - every object under its ID, an assembly as a clone without its components plus its
 *    main tool (+0x60) and main holder (+0x78) under their IDs - so in TlID::operator< order (tlcore 0x180004741,
 *    decomp/tlcore_tlserialize.c l.3234: Data1 u32, Data2 u16, Data3 u16, then Data4 bytes); then i32 + the slot map
 *    (TlMgr +0x30, std::map<long> = slot order) as { i32 slot, GUID }, then i32 + the lathe slot map (+0x40).
 *  - MFC CArchive (mfc140 ordinals as named in formats/mcam.md "Writer side"): WriteObject (Ordinal_14063) = null tag
 *    0x0000, a known object's index, or WriteClass(leaf) + a new map index + Serialize; SerializeClass (Ordinal_12658)
 *    = WriteClass: a known class 0x8000 | index, else 0xFFFF, u16 schema, u16 name length, the name, and a new index;
 *    classes and objects share one map counted from 1 (big tags: 0x7FFF then u32, bit 31 for a class, from index
 *    0x7FFF). CString (FUN_18035ee30 l.43175 of decomp/mccore_deserialize.c = AfxWriteStringLength + bytes): u8 length,
 *    0xFF + u16 from 255, 0xFF 0xFFFF + u32 from 0xFFFE.
 *  - Every level is kept with the schema the archive has, so an X9 archive is written with the X9 layouts (the
 *    DeserializeVn readers of the 2017 dll, the same as the X9 writers by CArchive symmetry) and a 2017 one with the
 *    2017 store branches. Bytes the readers discard (NOT DECODED) are kept as read.
 * Verified: every archive of the 16 user parts and the test exports, and 128 of 141 in Mastercam's shared / sample files
 * (the rest: lathe classes, pre-X9 schemas - refused), is rewritten
 * byte for byte (spec 10.3). */

enum { W_END, W_U8, W_I16, W_I32, W_F64, W_GUID, W_CSTR, W_RAW, W_BLOB, W_GLIST, W_OBJ, W_OPTOBJ, W_PROF, W_COOL, W_SEGS };
enum { M_SLOT = 0xf000, M_NONE, M_COOL, M_SEGS, M_MFR };    /* pseudo members: TlObject slot, discarded bytes, the
                                                                embedded coolant, the segment list, the manufacturer ID */
typedef struct { unsigned char k; unsigned short off, n; } FD;       /* kind, object member (spec 3), W_RAW length */

/* TlObject: v1 tlcore DeserializeV1 0x180005ad3 (tlcore_tlserialize.c l.509); v2 Serialize 0x18000cf9f l.1931-1938 */
static const FD FD_Object1[] = { { W_GUID, 0x30, 0 }, { W_CSTR, 0x18, 0 }, { W_CSTR, 0x20, 0 }, { W_END, 0, 0 } };
static const FD FD_Object2[] = { { W_GUID, 0x30, 0 }, { W_CSTR, 0x18, 0 }, { W_CSTR, 0x20, 0 }, { W_I32, M_SLOT, 0 }, { W_END, 0, 0 } };
/* TlAssemblyItem v7: DeserializeV7 0x1800027f2 (l.79) -> V6 (l.364) -> V5 (l.687) -> V4 (l.662) -> V3 (l.302) -> V2 (l.2864) */
static const FD FD_AItem7[] = {
    { W_CSTR, 0x48, 0 }, { W_CSTR, 0x50, 0 }, { W_CSTR, 0x58, 0 }, { W_CSTR, 0x60, 0 }, { W_I32, 0x80, 0 }, { W_U8, 0x84, 0 },
    { W_GUID, 0x88, 0 }, { W_GUID, 0x118, 0 }, { W_GUID, 0xa0, 0 }, { W_GUID, M_MFR, 0 }, { W_GLIST, 0x100, 0 },
    { W_U8, 0x85, 0 }, { W_GUID, 0xb8, 0 }, { W_U8, 0x86, 0 }, { W_F64, 0xd0, 0 }, { W_F64, 0xd8, 0 }, { W_BLOB, 0x68, 0 },
    { W_END, 0, 0 } };
/* v9: Serialize 0x18000d8aa l.2696-2752 */
static const FD FD_AItem9[] = {
    { W_CSTR, 0x48, 0 }, { W_CSTR, 0x50, 0 }, { W_CSTR, 0x58, 0 }, { W_CSTR, 0x60, 0 }, { W_I32, 0x80, 0 }, { W_U8, 0x84, 0 },
    { W_GUID, 0x88, 0 }, { W_GUID, 0xa0, 0 }, { W_GLIST, 0x100, 0 }, { W_U8, 0x85, 0 }, { W_GUID, 0xb8, 0 }, { W_U8, 0x86, 0 },
    { W_F64, 0xd0, 0 }, { W_F64, 0xd8, 0 }, { W_BLOB, 0x68, 0 }, { W_PROF, 0xe0, 0 }, { W_OPTOBJ, 0xf0, 0 }, { W_END, 0, 0 } };
/* TlTool v13: mccore DeserializeV13 0x1807511b0 (mccore_deserialize.c l.10352-10581); v15: Serialize 0x1807547b0
   (mccore_tlserialize.c l.11124-11172; the block is GetOpToolInfo, vfunc +0xa8) */
static const FD FD_Tool13[] = {
    { W_PROF, 0xe0, 0 }, { W_RAW, 0x158, LEGACY_N }, { W_I32, M_SLOT, 0 }, { W_I32, 0x130, 0 }, { W_I32, 0x134, 0 },
    { W_I32, 0x138, 0 }, { W_CSTR, 0x140, 0 }, { W_U8, 0x148, 0 }, { W_I32, 0x150, 0 }, { W_END, 0, 0 } };
static const FD FD_Tool15[] = {
    { W_RAW, 0x158, LEGACY_N }, { W_I32, 0x130, 0 }, { W_I32, 0x134, 0 }, { W_I32, 0x138, 0 }, { W_CSTR, 0x140, 0 },
    { W_U8, 0x148, 0 }, { W_I32, 0x150, 0 }, { W_I32, 0x14c, 0 }, { W_END, 0, 0 } };
/* TlToolMill v10: DeserializeV10 0x1808351b0 (mccore_deserialize.c l.18219-18728; the CString after +0x838 goes to
   TlAssemblyItem::SetGeometryFile = +0x60, the 16 bytes at l.18721 are read into a local: NOT DECODED);
   v12: Serialize 0x1808410f0 (mccore_tlserialize.c l.12025-12293) */
#define MILL_HEAD { W_OBJ, 0x888, 0 }, { W_F64, 0x878, 0 }, { W_F64, 0x880, 0 }, { W_F64, 0x7f0, 0 }, { W_F64, 0x7f8, 0 }, \
    { W_F64, 0x800, 0 }, { W_F64, 0x808, 0 }, { W_F64, 0x810, 0 }, { W_F64, 0x818, 0 }, { W_I32, 0x820, 0 }, \
    { W_F64, 0x828, 0 }, { W_F64, 0x830, 0 }, { W_I32, 0x840, 0 }, { W_U8, 0x86c, 0 }, { W_F64, 0x870, 0 }, { W_F64, 0x838, 0 }
#define MILL_MID { W_U8, 0x86b, 0 }, { W_I32, 0x8b8, 0 }, { W_I32, 0x850, 0 }, { W_I32, 0x854, 0 }, { W_I16, 0x858, 0 }, \
    { W_I32, 0x85c, 0 }, { W_F64, 0x860, 0 }, { W_CSTR, 0x848, 0 }, { W_U8, 0x86a, 0 }, { W_U8, 0x868, 0 }, { W_U8, 0x869, 0 }
static const FD FD_Mill10[] = { MILL_HEAD, { W_CSTR, 0x60, 0 }, { W_I32, 0x14c, 0 }, MILL_MID, { W_RAW, M_NONE, 0x10 },
    { W_RAW, 0x8f0, 0x78 }, { W_GLIST, 0x8c0, 0 }, { W_END, 0, 0 } };
static const FD FD_Mill12[] = { MILL_HEAD, MILL_MID, { W_RAW, 0x8f0, 0x78 }, { W_GLIST, 0x8c0, 0 }, { W_OPTOBJ, 0x8a8, 0 },
    { W_END, 0, 0 } };
#undef MILL_HEAD
#undef MILL_MID
/* tool subclasses: the Serialize store branches (mccore_tlserialize.c), the same schemas in the X9 parts (spec 3.5) */
static const FD FD_Endmill5[] = { { W_F64, 0x988, 0 }, { W_F64, 0x968, 0 }, { W_F64, 0x970, 0 }, { W_I32, 0x978, 0 },
    { W_F64, 0x980, 0 }, { W_END, 0, 0 } };                                             /* 0x180767a30 l.11344-11373 */
static const FD FD_Drill3[] = { { W_F64, 0x978, 0 }, { W_F64, 0x968, 0 }, { W_F64, 0x970, 0 }, { W_END, 0, 0 } };   /* 0x18075eee0 l.11271-11288 */
static const FD FD_Thread7[] = { { W_F64, 0x968, 0 }, { W_F64, 0x9a0, 0 }, { W_F64, 0x970, 0 }, { W_I32, 0x990, 0 },
    { W_F64, 0x978, 0 }, { W_F64, 0x980, 0 }, { W_F64, 0x988, 0 }, { W_I32, 0x994, 0 }, { W_I32, 0x998, 0 },
    { W_END, 0, 0 } };                                                                  /* 0x18085f450 l.12405-12478 */
static const FD FD_Boring2[] = { { W_F64, 0x980, 0 }, { W_I32, 0x990, 0 }, { W_F64, 0x988, 0 }, { W_END, 0, 0 } };  /* 0x18075a850 l.11216-11223 */
static const FD FD_CDrill1[] = { { W_F64, 0x980, 0 }, { W_F64, 0x988, 0 }, { W_END, 0, 0 } };   /* 0x180a2ff50 l.13995-14001 */
static const FD FD_CBore1[] = { { W_F64, 0x980, 0 }, { W_F64, 0x988, 0 }, { W_END, 0, 0 } };    /* 0x180a31870 l.14042-14048 */
static const FD FD_Slot2[] = { { W_I32, 0x990, 0 }, { W_F64, 0x998, 0 }, { W_F64, 0x9a0, 0 }, { W_F64, 0x9a8, 0 },
    { W_END, 0, 0 } };                                                                  /* 0x180a34000 l.14092-14115 */
static const FD FD_Reamer5[] = { { W_F64, 0x968, 0 }, { W_F64, 0x970, 0 }, { W_END, 0, 0 } };   /* 0x180855250 l.12317-12323 */
static const FD FD_Feed1[] = { { W_F64, 0x968, 0 }, { W_F64, 0x970, 0 }, { W_F64, 0x978, 0 }, { W_F64, 0x990, 0 },
    { W_F64, 0x980, 0 }, { W_F64, 0x988, 0 }, { W_END, 0, 0 } };                         /* 0x180a37a40 l.14156-14202 */
static const FD FD_Barrel1[] = { { W_F64, 0x968, 0 }, { W_F64, 0x970, 0 }, { W_F64, 0x978, 0 }, { W_END, 0, 0 } };   /* 0x180a3a3f0 l.14247-14260 */
/* TlHolder v4: DeserializeV4 0x1807281f0 (mccore_deserialize.c l.8518-8780: the 8 bytes at l.8749 are read into
   locals and dropped, NOT DECODED; the last CString -> SetGeometryFile = TlAssemblyItem +0x60); v5: Serialize
   0x18072add0 (mccore_tlserialize.c l.188-196) */
static const FD FD_Holder4[] = { { W_PROF, 0xe0, 0 }, { W_CSTR, 0x130, 0 }, { W_I32, 0x138, 0 }, { W_CSTR, 0x140, 0 },
    { W_CSTR, 0x150, 0 }, { W_I32, 0x148, 0 }, { W_I32, 0x158, 0 }, { W_RAW, M_NONE, 8 }, { W_CSTR, 0x60, 0 }, { W_END, 0, 0 } };
static const FD FD_Holder5[] = { { W_CSTR, 0x130, 0 }, { W_I32, 0x138, 0 }, { W_CSTR, 0x140, 0 }, { W_CSTR, 0x150, 0 },
    { W_I32, 0x148, 0 }, { W_I32, 0x158, 0 }, { W_END, 0, 0 } };
/* TlAssembly v8: DeserializeV8 0x180714060 (mccore_deserialize.c l.7250-7424); v9: Serialize 0x1807179d0
   (mccore_tlserialize.c l.59-124) */
static const FD FD_Assy8[] = { { W_I32, 0x48, 0 }, { W_GUID, 0x78, 0 }, { W_GUID, 0x60, 0 }, { W_I32, 0x4c, 0 }, { W_I32, 0x50, 0 },
    { W_I32, 0x54, 0 }, { W_F64, 0x90, 0 }, { W_U8, 0x58, 0 }, { W_I32, M_SLOT, 0 }, { W_F64, 0xa0, 0 }, { W_END, 0, 0 } };
static const FD FD_Assy9[] = { { W_I32, 0x48, 0 }, { W_GUID, 0x78, 0 }, { W_GUID, 0x60, 0 }, { W_I32, 0x4c, 0 }, { W_I32, 0x50, 0 },
    { W_I32, 0x54, 0 }, { W_F64, 0x90, 0 }, { W_U8, 0x58, 0 }, { W_F64, 0xa0, 0 }, { W_END, 0, 0 } };
/* tlcore (tlcore_tlserialize.c): TlProfile v2 DeserializeV2 0x1800027d9 l.32 / Serialize 0x18000f579 l.3223-3229 (u32 n,
   n x WriteObject); TlProfileSegment v1 0x180003f21 l.240 / 0x180003cf1 l.230 (u8 +0x48 colour); line v1 0x18000c351
   l.1383 / 0x18000a5a6 l.1304-1315; arc v1 0x1800011d6 l.6 / 0x180003a8a l.191-206; TlOpParams v1 0x18000d797 l.2409 /
   0x1800063ed l.634-655 (the embedded coolant +0x78 first); TlMillingOpParams v1 l.2176 / l.1357-1376; TlHolemaking v1
   l.1943 / l.788-807; TlCoolant v1 0x18000a380 l.814 / 0x18000f015 l.2778-2857 (21 x i32 +0x48..+0x98) */
static const FD FD_Profile2[] = { { W_SEGS, M_SEGS, 0 }, { W_END, 0, 0 } };
static const FD FD_Seg1[] = { { W_U8, 0x48, 0 }, { W_END, 0, 0 } };
static const FD FD_Line1[] = { { W_F64, 0x58, 0 }, { W_F64, 0x60, 0 }, { W_F64, 0x68, 0 }, { W_F64, 0x70, 0 }, { W_END, 0, 0 } };
static const FD FD_Arc1[] = { { W_F64, 0x70, 0 }, { W_F64, 0x78, 0 }, { W_F64, 0x68, 0 }, { W_F64, 0x58, 0 }, { W_F64, 0x60, 0 },
    { W_END, 0, 0 } };
static const FD FD_OpP1[] = { { W_COOL, M_COOL, 0 }, { W_F64, 0x48, 0 }, { W_F64, 0x50, 0 }, { W_F64, 0x58, 0 }, { W_F64, 0x68, 0 },
    { W_F64, 0x60, 0 }, { W_I32, 0x70, 0 }, { W_END, 0, 0 } };
static const FD FD_MillOpP1[] = { { W_U8, 0x88, 0 }, { W_U8, 0x89, 0 }, { W_F64, 0x90, 0 }, { W_F64, 0x98, 0 }, { W_F64, 0xa0, 0 },
    { W_F64, 0xa8, 0 }, { W_END, 0, 0 } };
static const FD FD_HoleOpP1[] = { { W_I32, 0x88, 0 }, { W_F64, 0x90, 0 }, { W_F64, 0x98, 0 }, { W_F64, 0xa0, 0 }, { W_F64, 0xa8, 0 },
    { W_F64, 0xb0, 0 }, { W_END, 0, 0 } };
static const FD FD_Coolant1[] = { { W_RAW, 0x48, 21 * 4 }, { W_END, 0, 0 } };
/* TlManufacturer v1 (tlcore Serialize 0x18000a0b0: no fields), TlToolGrade v1 (Serialize 0x18000fc6d: GUID +0x48) */
static const FD FD_Mfr1[] = { { W_END, 0, 0 } };
static const FD FD_Grade1[] = { { W_GUID, 0x48, 0 }, { W_END, 0, 0 } };
static const struct { int lev, schema; const FD *fd; } LAYOUT[] = {
    { L_TlObject, 1, FD_Object1 }, { L_TlObject, 2, FD_Object2 }, { L_TlAssemblyItem, 7, FD_AItem7 },
    { L_TlAssemblyItem, 9, FD_AItem9 }, { L_TlTool, 13, FD_Tool13 }, { L_TlTool, 15, FD_Tool15 },
    { L_TlToolMill, 10, FD_Mill10 }, { L_TlToolMill, 12, FD_Mill12 }, { L_TlToolEndmill, 5, FD_Endmill5 },
    { L_TlToolDrill, 3, FD_Drill3 }, { L_TlToolThreading, 7, FD_Thread7 }, { L_TlToolBoring, 2, FD_Boring2 },
    { L_TlToolCenterDrill, 1, FD_CDrill1 }, { L_TlToolCounterBore, 1, FD_CBore1 }, { L_TlToolSlotMill, 2, FD_Slot2 },
    { L_TlToolReamer, 5, FD_Reamer5 }, { L_TlToolFeedMill, 1, FD_Feed1 }, { L_TlToolBarrelMill, 1, FD_Barrel1 },
    { L_TlHolder, 4, FD_Holder4 }, { L_TlHolder, 5, FD_Holder5 }, { L_TlAssembly, 8, FD_Assy8 }, { L_TlAssembly, 9, FD_Assy9 },
    { L_TlProfile, 2, FD_Profile2 }, { L_TlProfileSegment, 1, FD_Seg1 }, { L_TlProfileLineSegment, 1, FD_Line1 },
    { L_TlProfileArcSegment, 1, FD_Arc1 }, { L_TlOpParams, 1, FD_OpP1 }, { L_TlMillingOpParams, 1, FD_MillOpP1 },
    { L_TlHolemakingOpParams, 1, FD_HoleOpP1 }, { L_TlCoolant, 1, FD_Coolant1 }, { L_TlManufacturer, 1, FD_Mfr1 },
    { L_TlToolGrade, 1, FD_Grade1 },
};
static const FD *layout_of(int lev, int schema) {
    for (size_t i = 0; i < sizeof LAYOUT / sizeof *LAYOUT; i++) if (LAYOUT[i].lev == lev && LAYOUT[i].schema == schema) return LAYOUT[i].fd;
    return NULL;
}

/* the object model: every level with every item as read (bytes kept), objects by pointer */
typedef struct WO WO;
typedef struct { unsigned char k; unsigned short off; uint32_t n; uint8_t *p; WO *o; WO **v; int nv; } WI;
typedef struct { int lev, schema; WI *it; int nit; } WL;
struct WO { int leaf; WL lv[8]; int nlv; uint32_t widx; };
typedef struct {
    WO **all; int nall;                          /* every object (to free) */
    struct { uint8_t key[16]; WO *o; } *item; int nitem;
    uint8_t *slots; int nslots;                  /* slot map entries as read: 20 bytes each (i32 slot, GUID) */
    uint8_t *lathe; int nlathe;
} WM;

static WO *wo_new(WM *w, int leaf) {
    WO *o = xcalloc(1, sizeof *o); o->leaf = leaf;
    w->all = xrealloc(w->all, sizeof *w->all * (w->nall + 1)); w->all[w->nall++] = o;
    return o;
}
static WI *wl_add(WL *l, int k, int off) {
    l->it = xrealloc(l->it, sizeof *l->it * (l->nit + 1));
    WI *i = &l->it[l->nit++]; memset(i, 0, sizeof *i); i->k = (unsigned char)k; i->off = (unsigned short)off;
    return i;
}
static void wi_bytes(WI *i, const void *src, uint32_t n) { free(i->p); i->p = xmalloc(n ? n : 1); if (n) memcpy(i->p, src, n); i->n = n; }
static void wm_free(WM *w) {
    for (int i = 0; i < w->nall; i++) {
        WO *o = w->all[i];
        for (int l = 0; l < o->nlv; l++) { for (int k = 0; k < o->lv[l].nit; k++) { free(o->lv[l].it[k].p); free(o->lv[l].it[k].v); } free(o->lv[l].it); }
        free(o);
    }
    free(w->all); free(w->item); free(w->slots); free(w->lathe);
    memset(w, 0, sizeof *w);
}

/* ---- reading into the model (the same grammar as parse(), every byte kept) ---- */
static void w_levels(Ar *a, WM *w, WO *o, int ch);
static WO *w_read_object(Ar *a, WM *w) {
    uint32_t bit, t = tag(a, &bit);
    if (a->err || t == 0) return NULL;
    int i;
    if (t == 0xffff) i = new_class(a);
    else if (t & bit) i = class_ref(a, t, bit);
    else {                                          /* back-reference */
        if (t >= (uint32_t)a->nm || !a->m[t].wo) { a->err = 1; return NULL; }
        return a->m[t].wo;
    }
    if (a->err) return NULL;
    int ch = chain_of(a->m[i].lev);
    if (ch < 0 || a->depth >= MAX_DEPTH) { a->err = 1; return NULL; }
    WO *o = wo_new(w, a->m[i].lev);
    int k = add_map(a, -1, 0, NULL); a->m[k].wo = o;
    a->depth++; w_levels(a, w, o, ch); a->depth--;
    return o;
}
static WO *w_embedded(Ar *a, WM *w, int ch) {      /* GetStoredProfile / coolant: Serialize only, no map index */
    if (a->depth >= MAX_DEPTH) { a->err = 1; return NULL; }
    WO *o = wo_new(w, CHAIN[ch].lv[1]);
    a->depth++; w_levels(a, w, o, ch); a->depth--;
    return o;
}
static uint32_t cstr_len(Ar *a) {                   /* the CString length prefix (cstr above) */
    uint32_t len = u8(a);
    if (len == 0xff) { len = u16(a); if (len == 0xfffe) { a->err = 1; return 0; } if (len == 0xffff) len = u32(a); }
    return len;
}
static void w_levels(Ar *a, WM *w, WO *o, int ch) {
    for (int k = 0; CHAIN[ch].lv[k] >= 0 && !a->err; k++) {
        int lev = CHAIN[ch].lv[k], v = level(a, lev);
        if (a->err) return;
        const FD *fd = layout_of(lev, v);
        if (!fd || o->nlv >= 8) { a->err = 1; return; }
        WL *L = &o->lv[o->nlv++]; L->lev = lev; L->schema = v;
        for (; fd->k != W_END && !a->err; fd++) {
            WI *it = wl_add(L, fd->k, fd->off);
            size_t at = a->p;
            switch (fd->k) {
            case W_U8: raw(a, NULL, 1); break;
            case W_I16: raw(a, NULL, 2); break;
            case W_I32: raw(a, NULL, 4); break;
            case W_F64: raw(a, NULL, 8); break;
            case W_GUID: raw(a, NULL, 16); break;
            case W_RAW: raw(a, NULL, fd->n); break;
            case W_CSTR: { uint32_t n = cstr_len(a); if (!need(a, n)) break; wi_bytes(it, a->b + a->p, n); a->p += n; continue; }
            case W_BLOB: { int32_t n = i32(a); if (n > 0) raw(a, NULL, (size_t)n); break; }
            case W_GLIST: guid_list(a); break;
            case W_OBJ: it->o = w_read_object(a, w); continue;
            case W_OPTOBJ: { unsigned f = u8(a); it->p = xmalloc(1); it->p[0] = (uint8_t)f; it->n = 1; if (f) it->o = w_read_object(a, w); continue; }
            case W_PROF: it->o = w_embedded(a, w, CH_PROFILE); continue;
            case W_COOL: it->o = w_embedded(a, w, CH_COOLANT); continue;
            case W_SEGS: {
                uint32_t n = u32(a);
                if (n > (a->n - a->p) / 2) { a->err = 1; break; }
                it->p = xmalloc(4); memcpy(it->p, a->b + at, 4); it->n = 4;
                it->v = xmalloc(sizeof *it->v * (n ? n : 1));
                for (uint32_t s = 0; s < n && !a->err; s++) {
                    WO *g = w_read_object(a, w);
                    if (!g || chain_of(g->leaf) < 0 || CHAIN[chain_of(g->leaf)].kind != K_SEG) { a->err = 1; break; }
                    it->v[it->nv++] = g;
                }
                continue;
            }
            }
            if (!a->err) wi_bytes(it, a->b + at, (uint32_t)(a->p - at));
        }
    }
}
static int w_parse(WM *w, const uint8_t *rec, size_t n, size_t *start) {
    static const uint8_t TAG[11] = { 0xff, 0xff, 5, 0, 5, 0, 'T', 'l', 'M', 'g', 'r' };
    Ar a; memset(&a, 0, sizeof a); memset(w, 0, sizeof *w);
    size_t k = 0;
    while (k + sizeof TAG <= n && memcmp(rec + k, TAG, sizeof TAG)) k++;
    if (k + sizeof TAG > n) return -1;
    *start = k;
    a.b = rec + k; a.n = n - k;
    add_map(&a, -1, 0, NULL);
    if (level(&a, L_TlMgr) != 5) { free(a.m); return -1; }
    int ni = count(&a, 18);
    w->item = xcalloc(ni ? ni : 1, sizeof *w->item);
    for (int i = 0; i < ni && !a.err; i++) {
        raw(&a, w->item[i].key, 16);
        w->item[i].o = w_read_object(&a, w); w->nitem = i + 1;
        if (!w->item[i].o) a.err = 1;
    }
    int ns = count(&a, 20);
    if (!a.err) { w->slots = xmalloc((size_t)ns * 20 + 1); raw(&a, w->slots, (size_t)ns * 20); w->nslots = ns; }
    int nl = count(&a, 20);
    if (!a.err) { w->lathe = xmalloc((size_t)nl * 20 + 1); raw(&a, w->lathe, (size_t)nl * 20); w->nlathe = nl; }
    int bad = a.err || a.p != a.n;
    free(a.m);
    return bad ? -1 : 0;
}

/* ---- writing (CArchive store side, see the head of this section) ---- */
typedef struct { uint8_t *b; size_t n, cap; uint32_t count, cls[NLEV]; } WW;
static void ww_put(WW *o, const void *p, size_t n) {
    if (o->n + n > o->cap) { o->cap = (o->n + n) * 2 + 256; o->b = xrealloc(o->b, o->cap); }
    memcpy(o->b + o->n, p, n); o->n += n;
}
static void ww_u16(WW *o, unsigned v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; ww_put(o, b, 2); }
static void ww_u32(WW *o, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; ww_put(o, b, 4); }
static void ww_class(WW *o, int lev, int schema) {  /* WriteClass */
    if (o->cls[lev]) {
        if (o->cls[lev] < 0x7fff) ww_u16(o, 0x8000 | o->cls[lev]);
        else { ww_u16(o, 0x7fff); ww_u32(o, 0x80000000u | o->cls[lev]); }
        return;
    }
    size_t L = strlen(LEV[lev]);
    ww_u16(o, 0xffff); ww_u16(o, (unsigned)schema); ww_u16(o, (unsigned)L); ww_put(o, LEV[lev], L);
    o->cls[lev] = o->count++;
}
static void ww_cstr(WW *o, const uint8_t *s, uint32_t n) {   /* AfxWriteStringLength (non-unicode) + the bytes */
    uint8_t b;
    if (n < 255) { b = (uint8_t)n; ww_put(o, &b, 1); }
    else if (n < 0xfffe) { b = 0xff; ww_put(o, &b, 1); ww_u16(o, n); }
    else { b = 0xff; ww_put(o, &b, 1); ww_u16(o, 0xffff); ww_u32(o, n); }
    ww_put(o, s, n);
}
static void ww_levels(WW *o, WO *x);
static void ww_object(WW *o, WO *x) {               /* WriteObject */
    if (!x) { ww_u16(o, 0); return; }
    if (x->widx) { if (x->widx < 0x7fff) ww_u16(o, x->widx); else { ww_u16(o, 0x7fff); ww_u32(o, x->widx); } return; }
    ww_class(o, x->leaf, x->lv[x->nlv - 1].schema);
    x->widx = o->count++;
    ww_levels(o, x);
}
static void ww_levels(WW *o, WO *x) {               /* X::Serialize: Base::Serialize, SerializeClass(X), X's items */
    for (int l = 0; l < x->nlv; l++) {
        WL *L = &x->lv[l];
        ww_class(o, L->lev, L->schema);
        for (int k = 0; k < L->nit; k++) {
            WI *i = &L->it[k];
            switch (i->k) {
            case W_CSTR: ww_cstr(o, i->p, i->n); break;
            case W_OBJ: ww_object(o, i->o); break;
            case W_OPTOBJ: ww_put(o, i->p, 1); if (i->p[0]) ww_object(o, i->o); break;
            case W_PROF: case W_COOL: ww_levels(o, i->o); break;
            case W_SEGS: ww_u32(o, (uint32_t)i->nv); for (int s = 0; s < i->nv; s++) ww_object(o, i->v[s]); break;
            default: ww_put(o, i->p, i->n); break;
            }
        }
    }
}
static int guid_less(const uint8_t *a, const uint8_t *b) {   /* TlID::operator< (tlcore 0x180004741, l.3234) */
    uint32_t a1 = a[0] | a[1] << 8 | a[2] << 16 | (uint32_t)a[3] << 24, b1 = b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24;
    if (a1 != b1) return a1 < b1;
    unsigned a2 = a[4] | a[5] << 8, b2 = b[4] | b[5] << 8; if (a2 != b2) return a2 < b2;
    unsigned a3 = a[6] | a[7] << 8, b3 = b[6] | b[7] << 8; if (a3 != b3) return a3 < b3;
    return memcmp(a + 8, b + 8, 8) < 0;
}
static void w_emit(WM *w, WW *o) {
    static const uint8_t HDR[11] = { 0xff, 0xff, 5, 0, 5, 0, 'T', 'l', 'M', 'g', 'r' };   /* SerializeClass(TlMgr), schema 5 */
    for (int i = 0; i < w->nall; i++) w->all[i]->widx = 0;
    memset(o->cls, 0, sizeof o->cls); o->count = 1;
    ww_put(o, HDR, sizeof HDR); o->cls[L_TlMgr] = o->count++;
    for (int i = 1; i < w->nitem; i++)              /* the std::map order (insertion sort: the lists are short) */
        for (int j = i; j > 0 && guid_less(w->item[j].key, w->item[j - 1].key); j--) {
            uint8_t t[16]; WO *x = w->item[j].o;
            memcpy(t, w->item[j].key, 16); memcpy(w->item[j].key, w->item[j - 1].key, 16); memcpy(w->item[j - 1].key, t, 16);
            w->item[j].o = w->item[j - 1].o; w->item[j - 1].o = x;
        }
    ww_u32(o, (uint32_t)w->nitem);
    for (int i = 0; i < w->nitem; i++) { ww_put(o, w->item[i].key, 16); ww_object(o, w->item[i].o); }
    ww_u32(o, (uint32_t)w->nslots); ww_put(o, w->slots, (size_t)w->nslots * 20);
    ww_u32(o, (uint32_t)w->nlathe); ww_put(o, w->lathe, (size_t)w->nlathe * 20);
}

/* ---- edits: a part tool changed in this program, written back into its objects (spec 10.2) ---- */
static WI *w_item(WO *o, int lev, int off) {
    if (!o) return NULL;
    for (int l = 0; l < o->nlv; l++) if (o->lv[l].lev == lev)
        for (int k = 0; k < o->lv[l].nit; k++) if (o->lv[l].it[k].off == off) return &o->lv[l].it[k];
    return NULL;
}
static int w_set(WO *o, int lev, int off, const void *v, uint32_t n) {    /* a scalar item of that size */
    WI *i = w_item(o, lev, off);
    if (!i || i->n < n || !i->p) return 0;
    memcpy(i->p, v, n);
    return 1;
}
static void w_setd(WO *o, int lev, int off, double v) { w_set(o, lev, off, &v, 8); }
static void w_seti(WO *o, int lev, int off, int32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)((uint32_t)v >> 24) }; w_set(o, lev, off, b, 4); }
static double w_getd(WO *o, int lev, int off) { WI *i = w_item(o, lev, off); double d = 0; if (i && i->n == 8) memcpy(&d, i->p, 8); return d; }
static void w_sets(WO *o, int lev, int off, const char *s) { WI *i = w_item(o, lev, off); if (i && i->k == W_CSTR) wi_bytes(i, s, (uint32_t)strlen(s)); }
static WO *w_find(WM *w, const uint8_t *id, int leaf_kind) {
    for (int i = 0; i < w->nitem; i++)
        if (!memcmp(w->item[i].key, id, 16)) { int ch = chain_of(w->item[i].o->leaf); return ch >= 0 && CHAIN[ch].kind == leaf_kind ? w->item[i].o : NULL; }
    return NULL;
}

/* TlToolMill::CreateNewFromOldTool (mccore 0x180834f20, formats/mcam_write.md 8): the class of a mill tool type */
static int class_of_type(int type) {
    switch (type) {
    case 1: return L_TlToolCenterDrill;
    case 2: case 3: case 9: case 22: return L_TlToolDrill;
    case 4: case 5: case 24: return L_TlToolThreading;
    case 6: return L_TlToolReamer;
    case 7: return L_TlToolBoring;
    case 8: return L_TlToolCounterBore;
    case 14: return L_TlToolSlotMill;
    case 23: return L_TlToolFeedMill;
    case 25: return L_TlToolBarrelMill;
    case 10: case 11: case 12: case 13: case 15: case 16: case 17: case 18: case 19: case 21: return L_TlToolEndmill;
    case 0: case 20: return L_TlToolMill;
    default: return -1;
    }
}

/* Tool field -> object member: the inverse of fill() (spec 4); 'd' f64, 'i' i32, 'h' i16, 'b' u8, 's' CString */
typedef struct { int lev, off; char k; size_t to; } TF;
#define TD(l, o, f) { l, o, 'd', offsetof(Tool, f) }
#define TI(l, o, f) { l, o, 'i', offsetof(Tool, f) }
static const TF TF_COMMON[] = {
    { L_TlObject, 0x18, 's', offsetof(Tool, name) }, { L_TlObject, 0x20, 's', offsetof(Tool, desc) },   /* Name, Description */
    { L_TlAssemblyItem, 0x84, 'b', offsetof(Tool, is_metric) },                                       /* IsMetric */
    TI(L_TlTool, 0x130, num), TI(L_TlTool, 0x134, head_num), { L_TlTool, 0x140, 's', offsetof(Tool, mfg_code) },
    TD(L_TlToolMill, 0x7f0, oal), TD(L_TlToolMill, 0x7f8, shoulder_len), TD(L_TlToolMill, 0x800, flute_len),
    TD(L_TlToolMill, 0x808, dia), TD(L_TlToolMill, 0x810, arbor_dia), TD(L_TlToolMill, 0x818, shoulder_dia),
    TI(L_TlToolMill, 0x820, shank_type), TD(L_TlToolMill, 0x828, taper_len), TD(L_TlToolMill, 0x830, neck_dia),
    TI(L_TlToolMill, 0x840, spindle_dir), TD(L_TlToolMill, 0x838, req_pilot_dia), TI(L_TlToolMill, 0x8b8, type),
    TI(L_TlToolMill, 0x850, dia_off), TI(L_TlToolMill, 0x854, len_off), { L_TlToolMill, 0x858, 'h', offsetof(Tool, flutes) },
    TI(L_TlToolMill, 0x85c, helix_type), TD(L_TlToolMill, 0x860, helix_angle), { L_TlToolMill, 0x848, 's', offsetof(Tool, chuck) },
    { -1, 0, 0, 0 } };
/* the subclasses (spec 3.5; TaperAngle = the Tool's tip angle too for endmills / boring bars: fill()) */
static const TF TF_ENDMILL[] = { TD(L_TlToolEndmill, 0x988, corner_r), TD(L_TlToolEndmill, 0x968, taper_angle),
    TD(L_TlToolEndmill, 0x968, tip_angle), TD(L_TlToolEndmill, 0x970, tip_dia), TI(L_TlToolEndmill, 0x978, radius_type),
    TD(L_TlToolEndmill, 0x980, chamfer_len), TI(L_TlToolSlotMill, 0x990, sec_radius_type), TD(L_TlToolSlotMill, 0x998, sec_corner_r),
    TD(L_TlToolSlotMill, 0x9a0, sec_chamfer), TD(L_TlToolSlotMill, 0x9a8, sec_chamfer_angle), { -1, 0, 0, 0 } };
static const TF TF_DRILL[] = { TD(L_TlToolDrill, 0x978, corner_r), TD(L_TlToolDrill, 0x968, tip_angle),
    TD(L_TlToolDrill, 0x970, shoulder_angle), TD(L_TlToolCenterDrill, 0x980, drill_dia), TD(L_TlToolCenterDrill, 0x988, drill_len),
    TD(L_TlToolCounterBore, 0x980, pilot_dia), TD(L_TlToolCounterBore, 0x988, pilot_len), { -1, 0, 0, 0 } };
static const TF TF_BORING[] = { TD(L_TlToolDrill, 0x978, corner_r), TD(L_TlToolDrill, 0x970, shoulder_angle),
    TD(L_TlToolBoring, 0x980, taper_angle), TD(L_TlToolBoring, 0x980, tip_angle), TI(L_TlToolBoring, 0x990, radius_type),
    TD(L_TlToolBoring, 0x988, shift), { -1, 0, 0, 0 } };
static const TF TF_THREAD[] = { TD(L_TlToolThreading, 0x968, tip_angle), TD(L_TlToolThreading, 0x9a0, corner_r),
    TD(L_TlToolThreading, 0x970, pitch), TI(L_TlToolThreading, 0x990, radius_type), TD(L_TlToolThreading, 0x978, root_dia),
    TD(L_TlToolThreading, 0x980, thread_angle), TD(L_TlToolThreading, 0x988, tip_dia), TI(L_TlToolThreading, 0x994, nstarts),
    TI(L_TlToolThreading, 0x998, thread_shape), { -1, 0, 0, 0 } };
static const TF TF_REAMER[] = { TD(L_TlToolReamer, 0x968, tip_angle), TD(L_TlToolReamer, 0x970, chamfer_len), { -1, 0, 0, 0 } };
static const TF TF_FEED[] = { TD(L_TlToolFeedMill, 0x968, tip_dia), TD(L_TlToolFeedMill, 0x970, lower_r), TD(L_TlToolFeedMill, 0x978, upper_r),
    TD(L_TlToolFeedMill, 0x990, lower_taper), TD(L_TlToolFeedMill, 0x980, upper_taper), TD(L_TlToolFeedMill, 0x988, relief_taper),
    { -1, 0, 0, 0 } };
static const TF TF_BARREL[] = { TD(L_TlToolBarrelMill, 0x968, barrel_len), TD(L_TlToolBarrelMill, 0x970, profile_r),
    TD(L_TlToolBarrelMill, 0x978, corner_r), { -1, 0, 0, 0 } };
/* op params (TlToolMill +0x888, spec 3.8) */
static const TF TF_OPP[] = { TD(L_TlOpParams, 0x48, sfm_adj), TD(L_TlOpParams, 0x50, fpt_adj), TD(L_TlOpParams, 0x58, feed),
    TD(L_TlOpParams, 0x68, plunge), TD(L_TlOpParams, 0x60, retract), TI(L_TlOpParams, 0x70, rpm),
    { L_TlMillingOpParams, 0x88, 'b', offsetof(Tool, is_rough) }, { L_TlMillingOpParams, 0x89, 'b', offsetof(Tool, is_finish) },
    TD(L_TlMillingOpParams, 0x90, step_rxy), TD(L_TlMillingOpParams, 0x98, step_rz), TD(L_TlMillingOpParams, 0xa0, step_fxy),
    TD(L_TlMillingOpParams, 0xa8, step_fz), TI(L_TlHolemakingOpParams, 0x88, cycle), TD(L_TlHolemakingOpParams, 0x90, peck1),
    TD(L_TlHolemakingOpParams, 0x98, peck2), TD(L_TlHolemakingOpParams, 0xa0, peck_clear), TD(L_TlHolemakingOpParams, 0xa8, chipbreak),
    TD(L_TlHolemakingOpParams, 0xb0, dwell), { -1, 0, 0, 0 } };
#undef TD
#undef TI
static int tf_differs(const TF *f, const Tool *a, const Tool *b) {
    const char *p = (const char *)a + f->to, *q = (const char *)b + f->to;
    switch (f->k) {
    case 's': return strcmp(p, q) != 0;
    case 'd': return memcmp(p, q, sizeof(double)) != 0;
    default: return memcmp(p, q, sizeof(int)) != 0;
    }
}
/* writes the changed fields of TABLE; returns how many */
static int tf_apply(WO *o, const TF *t, const Tool *now, const Tool *base) {
    int n = 0;
    for (; t->lev >= 0; t++) {
        if (!tf_differs(t, now, base)) continue;
        const char *p = (const char *)now + t->to; int iv; memcpy(&iv, p, sizeof iv);
        switch (t->k) {
        case 's': w_sets(o, t->lev, t->off, p); break;
        case 'd': { double d; memcpy(&d, p, 8); w_setd(o, t->lev, t->off, d); break; }
        case 'i': w_seti(o, t->lev, t->off, iv); break;
        case 'h': { uint8_t b[2] = { (uint8_t)iv, (uint8_t)(iv >> 8) }; w_set(o, t->lev, t->off, b, 2); break; }
        case 'b': { uint8_t b = iv != 0; w_set(o, t->lev, t->off, &b, 1); break; }   /* the writers' bool: != 0 */
        }
        n++;
    }
    return n;
}

/* CoCreateGuid (TlObject::TlObject 0x180006bd6, tlcore_tlserialize.c l.3357): a random (version 4) GUID */
static void new_guid(uint8_t g[16]) {
    static uint64_t s; size_t got = 0;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { got = fread(g, 1, 16, f); fclose(f); }
    if (got != 16) {                        /* no /dev/urandom (Windows build): splitmix64 over time and a counter */
        if (!s) s = (uint64_t)time(NULL) ^ ((uint64_t)clock() << 32) ^ (uint64_t)(uintptr_t)g;
        for (int k = 0; k < 2; k++) {
            uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL; z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL; z ^= z >> 31;
            memcpy(g + 8 * k, &z, 8);
        }
    }
    g[7] = (uint8_t)((g[7] & 0x0f) | 0x40); g[8] = (uint8_t)((g[8] & 0x3f) | 0x80);   /* Data3 version 4, Data4[0] variant */
}
/* a new TlProfileLineSegment(x0, y0, x1, y1, colour 0) (tlcore 0x18000bd7f l.3465; TlProfileSegment colour +0x48 0 and
   TlObject() = new ID, empty name / description, slot -1: l.3403, l.3357), with the archive's TlObject schema */
static WO *new_line(WM *w, int obj_schema, const double *xy) {
    WO *g = wo_new(w, L_TlProfileLineSegment);
    WL *L = &g->lv[g->nlv++]; L->lev = L_TlObject; L->schema = obj_schema;
    uint8_t id[16]; new_guid(id);
    wi_bytes(wl_add(L, W_GUID, 0x30), id, 16); wi_bytes(wl_add(L, W_CSTR, 0x18), "", 0); wi_bytes(wl_add(L, W_CSTR, 0x20), "", 0);
    if (obj_schema == 2) { uint8_t m1[4] = { 0xff, 0xff, 0xff, 0xff }; wi_bytes(wl_add(L, W_I32, M_SLOT), m1, 4); }
    L = &g->lv[g->nlv++]; L->lev = L_TlProfileSegment; L->schema = 1;
    { uint8_t c = 0; wi_bytes(wl_add(L, W_U8, 0x48), &c, 1); }
    L = &g->lv[g->nlv++]; L->lev = L_TlProfileLineSegment; L->schema = 1;
    for (int k = 0; k < 4; k++) wi_bytes(wl_add(L, W_F64, 0x58 + 8 * k), &xy[k], 8);
    return g;
}
/* the holder outline (radius, height) points -> its stored profile (spec 3.7: line k = point k -> point k + 1). The
   lines keep their objects (TlProfileLineSegment::SetLine) when the count is unchanged and the profile has no arcs;
   otherwise the profile gets new line segments */
static void set_outline(WM *w, WO *holder, const double *rz, int n) {
    WI *pi = w_item(holder, L_TlHolder, 0xe0); if (!pi) pi = w_item(holder, L_TlAssemblyItem, 0xe0);
    if (!pi || !pi->o) return;
    WI *si = w_item(pi->o, L_TlProfile, M_SEGS); if (!si) return;
    int nl = n > 1 ? n - 1 : 0, inplace = si->nv == nl;
    for (int s = 0; s < si->nv && inplace; s++) if (si->v[s]->leaf != L_TlProfileLineSegment) inplace = 0;
    int schema = holder->lv[0].schema;
    if (!inplace) { free(si->v); si->v = xmalloc(sizeof *si->v * (nl ? nl : 1)); si->nv = 0; }
    for (int s = 0; s < nl; s++) {
        double xy[4] = { rz[2 * s], rz[2 * s + 1], rz[2 * s + 2], rz[2 * s + 3] };
        if (inplace) for (int k = 0; k < 4; k++) w_setd(si->v[s], L_TlProfileLineSegment, 0x58 + 8 * k, xy[k]);
        else si->v[si->nv++] = new_line(w, schema, xy);
    }
}

/* one tool: the objects of SLOT's assembly (spec 1.1) get NOW's fields that differ from BASE (spec 10.2) */
static int apply_edit(WM *w, PtEdit *e) {
    const Tool *now = e->now, *base = e->base;
    WO *A = NULL, *T = NULL, *H = NULL;
    for (int i = 0; i < w->nslots && !A; i++) {
        int32_t s; memcpy(&s, w->slots + 20 * i, 4);
        if (s == base->slot) A = w_find(w, w->slots + 20 * i + 4, K_ASSY);
    }
    if (!A) return PT_NOSLOT;
    WI *ti = w_item(A, L_TlAssembly, 0x60), *hi = w_item(A, L_TlAssembly, 0x78);
    if (ti) T = w_find(w, ti->p, K_TOOL);
    if (hi) H = w_find(w, hi->p, K_HOLDER);
    if (!T) return PT_NOSLOT;
    int r = 0, cls = class_of_type(now->type);
    if (cls != T->leaf) return PT_CLASS;     /* another tool class: a new object (CreateNewFromOldTool) NOT DONE */
    int n = tf_apply(T, TF_COMMON, now, base);
    const TF *leaf = NULL;
    switch (T->leaf) {
    case L_TlToolEndmill: case L_TlToolSlotMill: leaf = TF_ENDMILL; break;
    case L_TlToolDrill: case L_TlToolCenterDrill: case L_TlToolCounterBore: leaf = TF_DRILL; break;
    case L_TlToolBoring: leaf = TF_BORING; break;
    case L_TlToolThreading: leaf = TF_THREAD; break;
    case L_TlToolReamer: leaf = TF_REAMER; break;
    case L_TlToolFeedMill: leaf = TF_FEED; break;
    case L_TlToolBarrelMill: leaf = TF_BARREL; break;
    }
    /* the taper / tip angle pair: one member; the taper angle wins when both changed */
    Tool b2 = *base;
    if (leaf == TF_ENDMILL || leaf == TF_BORING) if (memcmp(&now->taper_angle, &base->taper_angle, 8)) b2.tip_angle = now->tip_angle;
    if (leaf) n += tf_apply(T, leaf, now, &b2);
    WI *opi = w_item(T, L_TlToolMill, 0x888);
    if (opi && opi->o) {
        n += tf_apply(opi->o, TF_OPP, now, base);
        if (now->coolant != base->coolant || now->coolant_style != base->coolant_style) {
            /* the block's +0x50 = TlCoolant IsOldStyle (TlToolMill::GetOpToolInfo 0x18083ff20, mcam_write.md 8) */
            WI *ci = w_item(opi->o, L_TlOpParams, M_COOL);
            if (ci && ci->o && e->block_now) { int16_t v; memcpy(&v, e->block_now + 0x50, 2); w_seti(ci->o, L_TlCoolant, 0x48, v); n++; }
        }
    }
    if (memcmp(&now->oal, &base->oal, 8)) {
        /* Mastercam's tool edit (spec 10.2): the wizard's "Overall length" is TruePhysicalLength
           (toolmanager.toolwizardlaunch TlDataInterop::ConvertMastercamMillTool l.19980); on OK
           ConvertExtensionsMillTool (l.19837-19840) does: if (!IsAdjustedForStickout || TruePhysicalLength != v)
           { TruePhysicalLength = v; OverallLength = v; }. The tool then goes back into its assembly
           (ToolPageBase::EditTool 0x180883900 -> tool system vfunc +0x1a8 = FUN_180a9d130 -> TlAssembly::OnSetMainTool
           0x180717710): adjusted (+0x86c != 0): Stickout (+0x90) = OverallLength, a TruePhysicalLength below it is
           raised to it; not adjusted: Stickout = OverallLength only when Stickout == 0 (+0x98 = 0 after load) */
        WI *ai = w_item(T, L_TlToolMill, 0x86c);
        int adj = ai && ai->n == 1 && ai->p[0];
        double v = now->oal, tr = w_getd(T, L_TlToolMill, 0x870);
        if (!adj || tr != v) { w_setd(T, L_TlToolMill, 0x870, v); w_setd(T, L_TlToolMill, 0x7f0, v); }
        double oal = w_getd(T, L_TlToolMill, 0x7f0);
        if (adj) {
            w_setd(A, L_TlAssembly, 0x90, oal);
            if (w_getd(T, L_TlToolMill, 0x870) - oal < 0) w_setd(T, L_TlToolMill, 0x870, oal);
        } else if (w_getd(A, L_TlAssembly, 0x90) == 0.0) w_setd(A, L_TlAssembly, 0x90, oal);
        n++;
    }
    if (now->num != base->num) w_seti(A, L_TlAssembly, 0x48, now->num);              /* TlAssembly ToolNumber */
    if (now->dia_off != base->dia_off) w_seti(A, L_TlAssembly, 0x50, now->dia_off);
    if (now->len_off != base->len_off) w_seti(A, L_TlAssembly, 0x54, now->len_off);
    WI *bi = w_item(T, L_TlTool, 0x158);                                   /* the op_tool_info block (spec 1.1) */
    if (bi && bi->n == LEGACY_N && e->block_now && e->block_base)
        for (int k = 0; k < LEGACY_N; k++) if (e->block_now[k] != e->block_base[k]) { bi->p[k] = e->block_now[k]; n++; }
    if (n) r |= PT_TOOL;
    if (H) {
        if (strcmp(now->holder, base->holder) && now->holder[0]) { w_sets(H, L_TlObject, 0x18, now->holder); r |= PT_HOLDER; }
        if (now->holder_n > 1 && (now->holder_n != base->holder_n || memcmp(now->holder_rz, base->holder_rz, sizeof(double) * 2 * now->holder_n))) {
            set_outline(w, H, now->holder_rz, now->holder_n); r |= PT_HOLDER;
        }
    }
    return r;
}

int part_tools_write(const uint8_t *rec, size_t n, PtEdit *ed, int ned, uint8_t **out, size_t *outn) {
    WM w; size_t start;
    *out = NULL; *outn = 0;
    if (w_parse(&w, rec, n, &start)) { wm_free(&w); return -1; }
    int changed = 0;
    for (int i = 0; i < ned; i++) { ed[i].result = apply_edit(&w, &ed[i]); if (ed[i].result > 0) changed++; }
    WW o; memset(&o, 0, sizeof o);
    ww_put(&o, rec, start);                         /* ent +0 .. the archive (spec 1: +0x68..+0x378 NOT DECODED, kept) */
    w_emit(&w, &o);
    wm_free(&w);
    *out = o.b; *outn = o.n;
    return changed;
}
