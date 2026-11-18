/* engrave.c - Mastercam 2017 Engrave (opcode 416): engrave.dll's driver and the EStamp7.exe engine, ported from their
 * decompiles (see engrave.h for the layers). Spec: ../formats/engrave.md (engrave.dll) and ../formats/engrave_engine.md
 * (EStamp7.exe). Citations: engrave.dll functions FUN_18xxxxxxx with decomp/engrave/<file>.c, EStamp7.exe functions
 * FUN_00xxxxxx with decomp/engrave/est/p_4b0000.c / p_460000.c line numbers (Ghidra, Delphi cspec, FDIV calls patched
 * inline; x87 formulas read from the disassembly at the cited addresses).
 * The engine works in single precision (f32 records, x87 float locals); this port uses double. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engrave.h"

#define S_U8(o) (in->S[o])
#define S_I32(o) (*(const int32_t *)(in->S + (o)))
static double s_f64(const uint8_t *S, int o) { double v; memcpy(&v, S + o, 8); return v; }
#define S_F64(o) s_f64(in->S, (o))

static void seterr(char *err, size_t errn, const char *msg) { if (err && errn) snprintf(err, errn, "%s", msg); }

/* ------------------------------------------------------------------------------------------------------------------ */
/* record lists                                                                                                         */

int eg_recs_add(EgRecs *r, int tag, double x, double y, double z) {
    if (r->n == r->cap) {
        int c = r->cap ? 2 * r->cap : 256;
        EgRec *p = realloc(r->r, (size_t)c * sizeof *p);
        if (!p) return 0;
        r->r = p; r->cap = c;
    }
    r->r[r->n].tag = tag; r->r[r->n].x = x; r->r[r->n].y = y; r->r[r->n].z = z; r->n++;
    return 1;
}
void eg_recs_free(EgRecs *r) { free(r->r); r->r = NULL; r->n = r->cap = 0; }
void eg_result_free(EgResult *r) {
    for (int i = 0; i < r->n; i++) eg_recs_free(&r->tp[i].rec);
    free(r->tp); r->tp = NULL; r->n = 0;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* engrave.dll: op record -> engine parameters (formats/engrave.md 3)                                                  */

/* KParamNormGr: size class of the contour box in mm (FUN_180046f80, decomp/engrave/core6.c l.119-168) */
static int param_norm_gr(double dx, double dy) {
    double m = dx > dy ? dx : dy;
    return m < 75 ? 1 : m < 150 ? 2 : m < 350 ? 3 : 4;
}

int engrave_setup(const EngraveIn *in, EgEstDat *e, EgLink *lk, char *err, size_t errn) {
    memset(e, 0, sizeof *e); memset(lk, 0, sizeof *lk);
    if (memcmp(in->S + 0x128, "ENGR", 4)) { seterr(err, errn, "engrave: the op's Engrave block has no ENGR magic (S+0x128)"); return 0; }
    const double f = in->metric ? 1.0 : 25.4;          /* FUN_180052250 l.105-111: inch parts x25.4 into the engine */
    lk->f = f;
    /* heights, FUN_18001bb30 (decomp/engrave/heights.c): z0 = plane origin z, zc = chain z */
    double z0 = in->plane_z;
    double ztop = (S_I32(0x38) == 0 ? z0 : in->chain_z) + S_F64(0x58);                 /* +0x33c0 */
    double zdep = S_I32(0x30) == 0 ? z0 + S_F64(0x60) : ztop + S_F64(0x60);            /* +0x33c8 */
    double zfeed = S_I32(0x34) == 0 ? z0 + S_F64(0x68) : ztop + S_F64(0x68);           /* +0x33d0 */
    double zclr = S_I32(0x3c) == 0 ? z0 + S_F64(0x48) : ztop + S_F64(0x48);            /* +0x33d8 */
    double zret = S_I32(0x40) == 0 ? z0 + S_F64(0x50) : ztop + S_F64(0x50);            /* +0x33e0 */
    double depth = ztop - zdep, fly = zfeed - ztop;                                     /* row +0x10 / +0x18 */
    /* tool geometry (heights.c l.40-77): types 12 / 21 tip radius = tip dia / 2, angle = tool angle, major r = dia / 2 */
    double tip_r, ang, major_r;
    if (in->tool_type == 12 || in->tool_type == 21) { tip_r = in->tool_tip_dia * 0.5; ang = in->tool_angle; major_r = in->tool_dia * 0.5; }
    else if (in->tool_type == 13 || in->tool_type == 17) {
        seterr(err, errn, "engrave: tool types 13 / 17 (their major radius uses tool fields not read here) are not ported"); return 0;
    } else if (fabs(S_F64(0xd8)) >= 1e-12) { tip_r = in->tool_dia * 0.5; ang = S_F64(0xd8); major_r = in->tool_dia * 0.5 * 100.0; }
    else { tip_r = in->tool_dia * 0.5; ang = 0; major_r = in->tool_dia * 0.5; }
    if (depth <= 0) { seterr(err, errn, "engrave: the current top of stock or depth values are invalid (engraveres 15)"); return 0; }
    if (tip_r * (in->metric ? 1 : 25.4) <= 1e-4) { seterr(err, errn, "engrave: the tip diameter can not be zero (engraveres 16)"); return 0; }
    if (ang != 0 && (major_r - tip_r) / tan(ang * M_PI / 180) < depth) {
        seterr(err, errn, "engrave: conical part of tool is too short for the given cutting depth (dialog 1808 not ported)"); return 0;
    }
    if (S_I32(0x90)) { seterr(err, errn, "engrave: remachining is not ported"); return 0; }
    if (S_I32(0xe4) == 1) { seterr(err, errn, "engrave: wrapping is not ported"); return 0; }
    if (fabs(S_F64(0xc8)) > 1e-12) { seterr(err, errn, "engrave: XY stock to leave (its tool-radius adjustment) is not ported"); return 0; }

    /* EST_DAT [Parameter] (FUN_18004f870, decomp/engrave/strrefs1.c l.19-648; constants FUN_18002d970 core4.c l.1023-1046) */
    e->bahnopt = S_U8(0x13) != 0;
    e->bo_min_dist = 0.05 / f;          /* written unscaled in part units: 0.001969 for inch parts (the shipped sample) */
    e->bo_min_pts = 4; e->bo_close = 1; e->bo_vec_res = 10000; e->circle_res = 26; e->spline_res = 10; e->dxf_scale = 1;
    e->round_angle = S_I32(0xd0) == 1 ? 20.0 : 150.0;   /* P+0xe8 = (S+0xd0 == 1): Sharp 20, None 150 */
    e->conventional = S_U8(0x00) != 0;
    e->overlap = S_F64(0x08);
    e->datacheck = 1; e->start_inside = 1; e->connect_lines = 1; e->abr_merge = 0; e->abr_round = 0;
    e->blockwise = S_I32(0x9c) + 1;     /* P+0x68: 0 -> 1, 1 -> 2, 2 -> 3 */
    e->abr_first = S_U8(0x10) != 0;
    e->resort = S_U8(0x13) != 0;
    { int sp = S_I32(0x130); e->start_point = sp == 0 ? 2 : sp == 1 ? 1 : 3; }   /* P+0x188 */
    e->box[0] = 0; e->box[1] = 0; e->box[2] = 100 * f; e->box[3] = 100 * f;          /* Grenze = CoreParams +0x10..+0x28 */
    /* [Werkzeuge]: one tool (FUN_180050fd0, strrefs1.c l.652-1040) */
    e->ntools = 1;
    EgEstTool *t = &e->tool[0];
    snprintf(t->name, sizeof t->name, "%s", "ENGRAVE");
    t->depth = f * depth; t->fly = f * fly; t->tip_r = f * tip_r; t->angle = ang; t->shank_r = f * major_r;
    t->feed = in->feed; t->plunge = in->plunge; t->rpm = in->rpm; t->change = 1;
    /* KorrekturVerf = W.surface (S+0x20) ? core tool +0x94 : 0 "2D". The value of +0x94 is NOT DECODED (FUN_18004ed10 ->
       FUN_18002ddd0); "2D3D" for a V tool is what the shipped EST_DAT.TXT sample and the coaster op 2 NCI (3D corner moves) show. */
    if (S_U8(0x20)) {
        if (ang == 0) { seterr(err, errn, "engrave: KorrekturVerf for 'on top' with a 0 deg tool is not decoded"); return 0; }
        t->korr = '3';
    } else t->korr = '2';
    t->layers = S_I32(0xa0) ? S_I32(0xa8) : 1;
    t->split = S_I32(0xac) == 1 ? 'V' : 'L';
    t->ramp = S_I32(0xa4) ? S_F64(0xb0) : 90.0;
    /* [Berechnung] */
    if (!S_U8(0x11)) e->abr = 'K';
    else { int rt = S_I32(0x14); if (rt == 4) rt = 1; e->abr = rt == 0 ? 'Z' : rt == 1 ? 'Y' : rt == 2 ? 'C' : 'D'; }
    e->abr_angle = S_F64(0x18);
    e->contours_on = S_U8(0x20) ? 'S' : 'G';
    e->engrave_type = 'V';
    e->spant_on = 1; e->spant_spiegel = 0;
    e->abr_layers = t->layers; e->abr_split = t->split; e->abr_ramp = 90.0;   /* SpantAbr* from W (no remachining) */

    /* link values (FUN_18004aea0 / FUN_18004abf0, decomp/engrave/core6.c l.169-384) */
    lk->top = ztop; lk->depth = depth; lk->fly = fly;
    lk->feed_z = zfeed; lk->clear_z = zclr; lk->retr_z = zret;
    lk->clr_on = S_I32(0x70) != 0; lk->retr_on = S_I32(0x74) != 0; lk->clr_startend = S_I32(0xc0) != 0;
    lk->rapid_on = lk->retr_on || (!lk->clr_startend && lk->clr_on);
    lk->rapid_z = 0;
    if (!lk->clr_startend && lk->clr_on && lk->retr_on) lk->rapid_z = zclr > zret ? zclr : zret;
    else if (!lk->clr_startend && lk->clr_on) lk->rapid_z = zclr;
    else if (lk->retr_on) lk->rapid_z = zret;
    lk->p180 = in->op141 != 0;
    lk->feed = in->feed; lk->plunge = in->plunge; lk->retract = in->retract;
    return 1;
}

/* ------------------------------------------------------------------------------------------------------------------ */
/* engrave.dll: ENGTOOLP.LG1 records -> moves (FUN_180041910, decomp/engrave/contour_build.c l.352-728)                 */

typedef struct { Move *m; int n, cap; } MvList;
static void mv_add(MvList *L, double x, double y, double z, int type, double feed) {
    if (L->n == L->cap) { L->cap = L->cap ? 2 * L->cap : 256; L->m = realloc(L->m, (size_t)L->cap * sizeof *L->m); }
    Move *q = &L->m[L->n++]; memset(q, 0, sizeof *q);
    q->rapid = type == 0; q->p.x = x; q->p.y = y; q->p.z = z; q->feed = type == 0 ? -2.0 : feed;
}
/* feed choice FUN_180046920 (contour_build.c l.739-757): same xy and z rising -> retract feed (2), same xy otherwise ->
   plunge feed (3), else feed (1) */
static double feed_choice(const EgLink *lk, const double *a, const double *b, int *type) {
    if (fabs(a[0] - b[0]) <= 1e-12 && fabs(a[1] - b[1]) <= 1e-12) {
        if (1e-12 < b[2] - a[2]) { *type = 2; return lk->retract; }
        *type = 3; return lk->plunge;
    }
    *type = 1; return lk->feed;
}

int eg_lg1_to_moves(const EgResult *r, const EgLink *lk, Move **m, int *nm) {
    MvList L = { 0 };
    int first = 1, have = 0, prevtag = -1;
    double cur[3] = { 0, 0, 0 }, prev[3] = { 0, 0, 0 }, pxy[2] = { 0, 0 };
    double fsel = lk->feed;
    for (int t = 0; t < r->n; t++) {
        const EgRecs *R = &r->tp[t].rec;
        for (int i = 0; i < R->n; i++) {
            const EgRec *q = &R->r[i];
            int tag = q->tag, type;
            double x = q->x / lk->f, y = q->y / lk->f, z = q->z / lk->f, zz;
            if (tag != 0x0a && tag != 0x0b && tag != 0x0c) { free(L.m); return 0; }   /* FUN_18005ccc0: other tags -> error 12 */
            fsel = lk->feed;                                                         /* local_350 = P+0x168 per record */
            if (tag == 0x0c) zz = -z;
            else if (tag == 0x0a) {
                zz = fabs(z) > 1e-6 ? -z : -lk->depth;
                if (prevtag == 0x0b) {          /* plunge: (previous x, y) at the cut depth */
                    double p[3] = { pxy[0], pxy[1], -lk->depth + lk->top };
                    if (have) fsel = feed_choice(lk, prev, p, &type);
                    mv_add(&L, p[0], p[1], p[2], 1, fsel);
                    prev[0] = p[0]; prev[1] = p[1]; prev[2] = p[2]; have = 1;
                }
            } else zz = lk->fly - z;             /* 0x0b */
            zz += lk->top;
            cur[0] = x; cur[1] = y; cur[2] = zz;
            if (first && lk->clr_on) mv_add(&L, x, y, lk->clear_z, 0, 0);   /* the first record: clearance */
            first = 0;
            int ftype = tag != 0x0b;              /* local_338 */
            if (have) {
                fsel = feed_choice(lk, prev, cur, &type);
                if (!lk->p180) {
                    if (!ftype && prev[2] < zz - 1e-12) {
                        mv_add(&L, prev[0], prev[1], zz, 1, lk->retract);
                        prev[2] = zz;
                        if (lk->rapid_on) { prev[2] = lk->rapid_z; mv_add(&L, prev[0], prev[1], lk->rapid_z, 0, 0); }
                    }
                } else if (!ftype && prev[2] < zz - 1e-12) {
                    prev[2] = zz;
                    if (lk->rapid_on) { prev[2] = lk->rapid_z; mv_add(&L, prev[0], prev[1], lk->rapid_z, 0, 0); }
                }
            }
            mv_add(&L, x, y, zz, ftype, fsel);
            prev[0] = x; prev[1] = y; prev[2] = zz; have = 1;
            pxy[0] = x; pxy[1] = y; prevtag = tag;
        }
    }
    if (have) {                                   /* the end: feed plane, then retract / clearance */
        mv_add(&L, prev[0], prev[1], lk->feed_z, lk->p180 ? 0 : 1, lk->p180 ? 0 : lk->retract);
        if (lk->retr_on || lk->clr_on) {
            double z = prev[2];
            if (lk->retr_on) z = lk->retr_z;
            if (lk->clr_on) z = lk->clear_z;
            mv_add(&L, prev[0], prev[1], z, 0, 0);
        }
    }
    *m = L.m; *nm = L.n;
    return 1;
}


/* ================================================================================================================== */
/* the 2D offset back end: iso-level extraction FUN_00501ec4, join FUN_00503ed4, retry FUN_0051001c (formats/engrave_engine.md 4)
   (port of eg_offset.c, decoded from EStamp7.exe; renamed file-scope names: off_*) */
#define Core off_Core
#define Elem off_Elem
#define Join off_Join
#define Line off_Line
#define Piece off_Piece
#define Pt off_Pt
#define chain off_chain
#define clamp_to_seg off_clamp_to_seg
#define copy_piece off_copy_piece
#define eg_recs_free off_eg_recs_free
#define eg_recs_push off_eg_recs_push
#define eg_recs_push_rec off_eg_recs_push_rec
#define fdist off_fdist
#define grow off_grow
#define in_extent off_in_extent
#define join_pass off_join_pass
#define k_tol_table off_k_tol_table
#define line_axis off_line_axis
#define line_isect off_line_isect
#define line_to off_line_to
#define normal_off off_normal_off
#define recs_push off_recs_push
#define w_main off_w_main
#define w_var off_w_var
/* eg_offset.h - port of the EStamp7 2D contour offset ("Korrektur") back end.
 *
 * Sources (EStamp7p.exe, decompile p_4b0000.c):
 *   FUN_0051001c  retry wrapper            -> eg_offset_retry()
 *   FUN_0050443c  core + join              -> eg_offset_443c()
 *   FUN_005041dc / FUN_00501ec4 core       -> eg_offset_core()
 *   FUN_00503ed4  join pieces into loops   -> eg_offset_join()
 *
 * IMPORTANT: FUN_00501ec4 does NOT read the source contour.  It reads three
 * temp files written once per tool pass by the precomputation FUN_00501550
 * (called via FUN_005041a4("VekAR1.TMP", side) from FUN_00528dac, dump line
 * 87211):
 *   VekAR7.TMP  25-byte records (tag u8 + 3 doubles): the contours, tag 0x0b
 *               start / 0x0a line-to, plus one trailing 0x0b sentinel
 *               (FUN_004fcd88 @ 53770).  Contours are implicitly closed.
 *   VekAR6.TMP  25-byte records: per segment a polyline of "envelope" samples
 *               (x, y, z = d) where d = distance of the sample from the
 *               segment line along its offset normal divided by W
 *               (FUN_004ffb0c @ 56030-56058).
 *   VekAR5.TMP  int32 cumulative sample index: entry k = number of VekAR6
 *               records before segment k; one extra entry at the end
 *               (FUN_005013cc @ 57357, 57386).
 * That precomputation (FUN_00501550 and its ~2000-line subtree) is NOT
 * DECODED here; the caller has to supply its output in EgOffsetPre.
 */


/* Output of the precomputation FUN_00501550 (see above). */
typedef struct {
    const EgRecs *ar7;   /* VekAR7.TMP */
    const EgRecs *ar6;   /* VekAR6.TMP (z = normalised height d) */
    const int *ar5;      /* VekAR5.TMP */
    int n5;
} EgOffsetPre;

typedef struct {
    int side_flag;   /* param_3 of FUN_00501ec4: offset normal side = side_flag ^ 1
                        passed to FUN_004fbfdc; 1 -> right of travel (dy,-dx) */
    int tol_index;   /* param_4: index into DAT_0052eafc (float table, .data) */
} EgOffsetParams;

/* engine error codes (*(int*)PTR_DAT_0052f230) */
#define EG_OFF_OK        0
#define EG_OFF_NODATA    100    /* 0x64: < 4 records (FUN_00503784, FUN_00503ed4) */
#define EG_OFF_PIECES    0x65   /* > 65535 pieces (FUN_00503784) */
#define EG_OFF_RETRY     0x66   /* piece not consumed (FUN_00503ed4 @ 59305) */
#define EG_OFF_GIVEUP    0x67   /* retries exhausted (FUN_0051001c @ 68771) */
/* port-only codes (no engine equivalent) */
#define EG_OFF_ERR_INPUT (-1001) /* inconsistent precomputed input */
#define EG_OFF_ERR_NOMEM (-1002)

void eg_recs_free(EgRecs *a);

/* FUN_005041dc -> FUN_00501ec4.  mode = param_6 (1 = plain offset pieces,
 * 0 = also emit envelope samples where the offset does not fit, z = d/r),
 * p7 = param_7 (> 0: second level written to *out_var, main output stays
 * empty), dist = param_8, W = param_9.  out_main receives the param_2 file
 * ("VekAR2.TMP"), out_var the param_1 file (only when p7 > 0; may be NULL
 * otherwise).  Coordinates are rounded to float32 as in the 13-byte files. */
int eg_offset_core(const EgOffsetPre *pre, EgRecs *out_main, EgRecs *out_var,
                   int mode, float p7, float dist, float W,
                   const EgOffsetParams *k);

/* FUN_00503ed4: join pieces (0x0b ... 0x0a runs) into loops. tol = dist. */
int eg_offset_join(const EgRecs *in, EgRecs *out, float tol);

/* FUN_0050443c: core(mode 1, p7 0, tol_index 1) + join(tol = dist). */
int eg_offset_443c(const EgOffsetPre *pre, EgRecs *out, int side_flag,
                   float dist, float W);

/* FUN_0051001c: retry wrapper around FUN_0050443c. */
int eg_offset_retry(const EgOffsetPre *pre, EgRecs *out, int side_flag,
                    float dist, float W);


/* eg_offset.c - port of the EStamp7 2D offset back end (HRKORRD unit).
 * See eg_offset.h and NOTES.md.  Dump line numbers refer to
 * <repo>/decomp/engrave/est/p_4b0000.c.
 *
 * Precision: the original keeps contour/sample coordinates in doubles, the
 * ratios r = dist/W, sample heights d and the interpolation parameter t in
 * float32, and writes float32 x,y,z to the 13-byte files.  x87 extended
 * intermediates are replaced by double (differences only in the last ulp).
 */

/* DAT_0052eafc (.data, read-only in the code; only reference @ dump 58031).
 * Entry 0 is not a float (bytes ff 7f 3f 1f, another table); 1..5 below. */
static const float k_tol_table[6] = {
    0.0f, 0.002f, 0.0025f, 0.001f, 0.0005f, 0.0002f
};

/* ------------------------------------------------------------------ */
void eg_recs_free(EgRecs *a)
{
    if (!a) return;
    free(a->r);
    a->r = NULL; a->n = a->cap = 0;
}

static int recs_push(EgRecs *a, int tag, double x, double y, double z)
{
    if (a->n == a->cap) {
        int nc = a->cap ? a->cap * 2 : 256;
        EgRec *p = realloc(a->r, (size_t)nc * sizeof *p);
        if (!p) return 0;
        a->r = p; a->cap = nc;
    }
    a->r[a->n].tag = tag; a->r[a->n].x = x; a->r[a->n].y = y; a->r[a->n].z = z;
    a->n++;
    return 1;
}

/* the 0x2e-byte line record used throughout the unit:
 * +1 axis flag, +6 x0, +0xe y0, +0x16 dx, +0x1e dy, +0x26 float slope */
typedef struct { int flag; double x0, y0, dx, dy; float slope; } Line;

/* FUN_004fbdec @ 52900: dominant axis + slope */
static void line_axis(Line *l)
{
    if (fabs(l->dx) <= fabs(l->dy)) {
        l->flag = 1;
        l->slope = (l->dy == 0.0) ? 0.0f : (float)(l->dx / l->dy);
    } else {
        l->flag = 0;
        l->slope = (l->dx == 0.0) ? 0.0f : (float)(l->dy / l->dx);
    }
}

/* FUN_004fbe68 @ 52929: direction := (x,y) - start */
static void line_to(Line *l, double x, double y)
{
    l->dx = x - l->x0;
    l->dy = y - l->y0;
    line_axis(l);
}

/* FUN_004fbf40 @ 52987: intersection of the infinite lines a and b, point
 * on a.  Returns 0 (and leaves *px,*py untouched) when parallel. */
static int line_isect(const Line *a, const Line *b, double *px, double *py)
{
    double den = a->dx * b->dy - a->dy * b->dx;               /* @53020 */
    if (den == 0.0) return 0;
    double t = ((b->x0 - a->x0) * b->dy - (b->y0 - a->y0) * b->dx) / den;
    *px = t * a->dx + a->x0;                                  /* @53026 */
    *py = t * a->dy + a->y0;
    return 1;
}

/* FUN_004fbfdc @ 53034 (dis 4fbff9-4fc050): normal of length dist.
 * side 0 -> (dy,-dx) (right of travel), side != 0 -> (-dy,dx). */
static void normal_off(const Line *l, int side, double dist, double *ox, double *oy)
{
    double k = dist / sqrt(l->dx * l->dx + l->dy * l->dy);
    if (side == 0) { *ox = l->dy * k;  *oy = -l->dx * k; }
    else           { *ox = -l->dy * k; *oy = l->dx * k; }
}

/* FUN_005017c0 @ 57481: P inside the extent of m along its dominant axis,
 * slack DAT_005018dc = 1e-6 (TBYTE). */
static int in_extent(double px, double py, const Line *m)
{
    const long double e = 1e-6L;
    long double p = m->flag ? py : px;
    long double o = m->flag ? m->y0 : m->x0;
    long double d = m->flag ? m->dy : m->dx;
    if (d >= 0.0L)
        return !((p <= o - e) || (e + o + d < p));
    else
        return !((e + o <= p) || (p <= (o + d) - e));
}

/* FUN_00501d80 @ 57754: clamp P to the segment s along its dominant axis */
static void clamp_to_seg(double *px, double *py, const Line *s)
{
    double p = s->flag ? *py : *px;
    double o = s->flag ? s->y0 : s->x0;
    double d = s->flag ? s->dy : s->dx;
    int to_end = 0, to_start = 0;
    if (d >= 0.0) {
        if (o <= p) { if (o + d < p) to_end = 1; }
        else to_start = 1;
    } else {
        if (p <= o) { if (p < o + d) to_end = 1; }
        else to_start = 1;
    }
    if (to_end)   { *px = s->x0 + s->dx; *py = s->y0 + s->dy; }
    if (to_start) { *px = s->x0;         *py = s->y0; }
}

/* ------------------------------------------------------------------ */
/* FUN_00501ec4 state (frame locals of the original) */
typedef struct { double x, y, dx, dy; float d; } Elem;   /* 0x2e stride, buf local_6e8 */
typedef struct { double x, y; } Pt;

typedef struct {
    EgRecs *out_main, *out_var;
    int var;                 /* local_559 = p7 > 0 */
    int first_seg;           /* local_55a */
    float main_lx, main_ly;  /* last record in local_560 buffer */
    float var_lx, var_ly;    /* last record in local_6b4 buffer */
    float var_tol;           /* local_6bc = DAT_0052eafc[param_4] */
    int nomem;
} Core;

/* FUN_00501a54 @ 57622: main writer (13-byte records, float32), drops a
 * point within 0.001 (DAT_00501bd4, TBYTE) of the last one unless this is
 * the first segment of a contour; writes nothing in variable mode. */
static void w_main(Core *c, int tag, double x, double y, double z)
{
    if (c->var) return;
    if (c->first_seg || c->out_main->n == 0 ||
        0.001L < fabsl((long double)c->main_lx - (long double)x) ||
        0.001L < fabsl((long double)c->main_ly - (long double)y)) {
        float fx = (float)x, fy = (float)y, fz = (float)z;
        if (!recs_push(c->out_main, tag, fx, fy, fz)) { c->nomem = 1; return; }
        c->main_lx = fx; c->main_ly = fy;
        /* progress display FUN_004bc3d8/FUN_004bc3ec omitted */
    }
}

/* FUN_00501c18 @ 57697: second writer (param_1 file), float tolerance */
static void w_var(Core *c, int tag, double x, double y, double z)
{
    float fx = (float)x, fy = (float)y;
    if (c->out_var->n == 0 ||
        c->var_tol < fabsf(c->var_lx - fx) ||
        c->var_tol < fabsf(c->var_ly - fy)) {
        if (!recs_push(c->out_var, tag, fx, fy, (float)z)) { c->nomem = 1; return; }
        c->var_lx = fx; c->var_ly = fy;
    }
}

static int grow(void **p, int *cap, int need, size_t sz)
{
    if (need < *cap) return 1;
    int nc = *cap ? *cap : 64;
    while (nc <= need) nc *= 2;
    void *q = realloc(*p, (size_t)nc * sz);
    if (!q) return 0;
    memset((char *)q + (size_t)*cap * sz, 0, (size_t)(nc - *cap) * sz);
    *p = q; *cap = nc;
    return 1;
}

/* FUN_00501ec4 @ 0x501ec4 (dump 57834-58903), entered via FUN_005041dc. */
int eg_offset_core(const EgOffsetPre *pre, EgRecs *out_main, EgRecs *out_var,
                   int mode, float p7, float dist, float W,
                   const EgOffsetParams *k)
{
    Core c;
    memset(&c, 0, sizeof c);
    out_main->n = 0;
    c.out_main = out_main;
    c.out_var = out_var;
    if (k->tol_index < 1 || k->tol_index > 5) return EG_OFF_ERR_INPUT;
    c.var_tol = k_tol_table[k->tol_index];                 /* @58031 */
    c.var = p7 > 0.0f;                                      /* @58051 */
    if (c.var) { if (!out_var) return EG_OFF_ERR_INPUT; out_var->n = 0; }
    float r2 = c.var ? (float)((double)p7 / (double)W) : 1.0f;  /* local_6c8 @58045 */
    float r  = (float)((double)dist / (double)W);           /* local_6c4 @58094 */
    int side = (k->side_flag & 1) ^ 1;                       /* local_6bd ^ 1 @58221 */

    const EgRecs *a7 = pre->ar7, *a6 = pre->ar6;
    if (!a7 || !a6 || !pre->ar5 || a7->n < 2 || pre->n5 < a7->n) return EG_OFF_ERR_INPUT;

    /* persistent buffers (stale contents are read by the original) */
    Elem *E = NULL; int Ecap = 0;
    EgRec *B = NULL; int Bcap = 0;            /* local_6d4, 25-byte records */
    Pt *Ad8 = NULL, *Adc = NULL, *Ae0 = NULL, *Ae4 = NULL;
    int cd8 = 0, cdc = 0, ce0 = 0, ce4 = 0;
    int n708 = 0, n70c = 0;                    /* down / up crossing counts */
    double Px = 0.0, Py = 0.0;                 /* local_ad4 / local_acc */
    double z933 = 0.0;                         /* local_933 */
    double offx = 0.0, offy = 0.0;             /* local_ae4 / local_adc */
    int rc = EG_OFF_OK;

    if (!grow((void **)&E, &Ecap, 2, sizeof *E)) return EG_OFF_ERR_NOMEM;

    int i7 = 0, i6 = 0, i5 = 0;
    EgRec start = a7->r[i7++];                 /* FUN_00501954 -> local_984 @58092 */
    EgRec cur = start;
    int cum = pre->ar5[i5++];                  /* FUN_005018e8 -> local_6ec @58093 */
    int newc = 1;                              /* local_71d */

    for (int seg = 0; seg < a7->n - 1; seg++) {   /* while local_6f4-1 != local_6fc @58816 */
        EgRec prev = newc ? start : cur;       /* local_9a4 @58100-58119 */
        cur = a7->r[i7++];                     /* local_964 @58120 */
        c.first_seg = newc;                    /* local_55a = local_71d @58121 */
        newc = 0;
        if (cur.tag == 0x0b) {                 /* @58123: close previous contour */
            EgRec t = cur; cur = start; start = t;
            newc = 1;
        }
        Line S;                                /* local_9d4 @58150-58156 */
        memset(&S, 0, sizeof S);
        S.x0 = prev.x; S.y0 = prev.y;
        S.dx = cur.x - prev.x; S.dy = cur.y - prev.y;
        line_axis(&S);

        int prevcum = cum;                     /* @58157-58160 */
        if (i5 >= pre->n5) { rc = EG_OFF_ERR_INPUT; goto done; }
        cum = pre->ar5[i5++];
        int n = cum - prevcum;
        if (n < 0 || i6 + n > a6->n) { rc = EG_OFF_ERR_INPUT; goto done; }
        if (!grow((void **)&E, &Ecap, n + 2, sizeof *E)) { rc = EG_OFF_ERR_NOMEM; goto done; }
        for (int i = 0; i < n; i++) {          /* @58161-58178 (sample tag ignored) */
            E[i].x = a6->r[i6].x; E[i].y = a6->r[i6].y;
            E[i].d = (float)a6->r[i6].z;
            i6++;
        }
        if (n > 0) n--;                        /* @58180: number of intervals */
        if (n == 0) {                          /* @58183-58194, dis 50235f-5023bd */
            float f = (float)((S.flag ? S.dy : S.dx) * 1000.0);
            E[1].x = S.dx / f + E[0].x;
            E[1].y = S.dy / f + E[0].y;
            E[1].d = E[0].d;
            n = 1;
        }
        for (int i = 0; i < n; i++) {          /* @58195-58207 */
            E[i].dx = E[i + 1].x - E[i].x;
            E[i].dy = E[i + 1].y - E[i].y;
        }

        int a721 = 0, n6f8 = 0, t724 = 0;      /* @58208-58212 */
        int i714 = n708, i718 = n70c;
        Line O = S, L, M, N;
        memset(&L, 0, sizeof L); memset(&N, 0, sizeof N);
        normal_off(&S, side, (double)dist, &offx, &offy);   /* @58221 */
        O.x0 += offx; O.y0 += offy;
        int st, i9;
        if (E[0].d <= r) {                     /* @58224: start below the level */
            if (mode == 0) {
                z933 = (double)E[0].d / (double)r;
                w_main(&c, 0x0b, E[0].x, E[0].y, z933);
            }
            st = 0;
        } else {                               /* @58240-58260: start above */
            L.x0 = S.x0; L.y0 = S.y0;
            line_to(&L, E[0].x, E[0].y);
            line_isect(&L, &O, &Px, &Py);      /* result flag ignored */
            z933 = 1.0;
            w_main(&c, 0x0b, Px, Py, z933);
            st = 1; a721 = 1;
        }
        i9 = 0;
        do {                                   /* @58262-58408 */
            int f = 0;
            if (st == 0) {
                do {
                    if (mode == 0) {
                        z933 = (double)E[i9].d / (double)r;
                        w_main(&c, 0x0a, E[i9].x, E[i9].y, z933);
                    }
                    if (E[i9].d <= r && r < E[i9 + 1].d) {          /* @58280 */
                        float t = (float)(((double)E[i9].d - r) / ((double)E[i9].d - E[i9 + 1].d));
                        Px = E[i9].dx * (double)t + E[i9].x;
                        Py = E[i9].dy * (double)t + E[i9].y;
                        f = 1;
                    }
                    i9++;
                } while (!f && i9 != n);
                if (f) {                                            /* @58293 */
                    z933 = 1.0;
                    w_main(&c, mode == 0 ? 0x0a : 0x0b, Px, Py, z933);
                    st = 1; a721 = 1;
                    if (c.var) {                                    /* @58311 */
                        if (!grow((void **)&Ae0, &ce0, n70c + 1, sizeof *Ae0) ||
                            !grow((void **)&Ae4, &ce4, n70c + 1, sizeof *Ae4)) { rc = EG_OFF_ERR_NOMEM; goto done; }
                        Ae0[n70c].x = Px; Ae0[n70c].y = Py;
                        Px -= offx; Py -= offy;
                        clamp_to_seg(&Px, &Py, &S);
                        Ae4[n70c].x = Px; Ae4[n70c].y = Py;
                        n70c++; n6f8++;
                        if (t724 == 0) t724 = 2;
                    }
                }
            } else {
                do {
                    if (r < E[i9].d && E[i9 + 1].d <= r) {          /* @58349 */
                        float t = (float)(((double)E[i9].d - r) / ((double)E[i9].d - E[i9 + 1].d));
                        Px = E[i9].dx * (double)t + E[i9].x;
                        Py = E[i9].dy * (double)t + E[i9].y;
                        f = 1;
                    }
                    i9++;
                } while (!f && i9 != n);
                if (f) {                                            /* @58362 */
                    z933 = 1.0;
                    w_main(&c, 0x0a, Px, Py, z933);
                    st = 0;
                    if (c.var) {                                    /* @58374 */
                        if (!grow((void **)&Ad8, &cd8, n708 + 1, sizeof *Ad8) ||
                            !grow((void **)&Adc, &cdc, n708 + 1, sizeof *Adc)) { rc = EG_OFF_ERR_NOMEM; goto done; }
                        Ad8[n708].x = Px; Ad8[n708].y = Py;
                        Px -= offx; Py -= offy;
                        clamp_to_seg(&Px, &Py, &S);
                        Adc[n708].x = Px; Adc[n708].y = Py;
                        n708++; n6f8++;
                        if (t724 == 0) t724 = 1;
                    }
                }
            }
        } while (i9 != n);
        if (st == 0) {                         /* @58409 */
            if (mode == 0) {
                z933 = (double)E[n].d / (double)r;
                w_main(&c, 0x0a, E[n].x, E[n].y, z933);
            }
        } else {                               /* @58421-58438 */
            L.x0 = S.x0 + S.dx; L.y0 = S.y0 + S.dy;
            L.dx = E[n].x - L.x0; L.dy = E[n].y - L.y0;
            if (0.0008L < fabsl((long double)L.dx) || 0.0008L < fabsl((long double)L.dy)) {
                line_axis(&L);
                line_isect(&L, &O, &Px, &Py);
                z933 = 1.0;
                w_main(&c, 0x0a, Px, Py, z933);
            }
        }

        if (c.var) {                           /* second level r2 @58440-58813 */
            int n704 = 0;
#define BPUSH(T) do { if (!grow((void **)&B, &Bcap, n704 + 2, sizeof *B)) { rc = EG_OFF_ERR_NOMEM; goto done; } \
                      z933 = 1.0; B[n704].tag = (T); B[n704].x = Px; B[n704].y = Py; B[n704].z = z933; n704++; } while (0)
            if (!grow((void **)&B, &Bcap, 2, sizeof *B)) { rc = EG_OFF_ERR_NOMEM; goto done; }
            O = S;
            normal_off(&S, side, (double)p7, &offx, &offy);  /* @58450 */
            O.x0 += offx; O.y0 += offy;
            if (E[0].d <= r2) st = 0;
            else {
                L.x0 = S.x0; L.y0 = S.y0;
                line_to(&L, E[0].x, E[0].y);
                line_isect(&L, &O, &Px, &Py);
                BPUSH(0x0b);
                st = 1;
            }
            i9 = 0;
            do {                               /* @58487-58564 */
                int f = 0;
                if (st == 0) {
                    do {
                        if (E[i9].d <= r2 && r2 < E[i9 + 1].d) {
                            float t = (float)(((double)E[i9].d - r2) / ((double)E[i9].d - E[i9 + 1].d));
                            Px = E[i9].dx * (double)t + E[i9].x;
                            Py = E[i9].dy * (double)t + E[i9].y;
                            f = 1;
                        }
                        i9++;
                    } while (!f && i9 != n);
                    if (f) { BPUSH(0x0b); st = 1; }
                } else {
                    do {
                        if (r2 < E[i9].d && E[i9 + 1].d <= r2) {
                            float t = (float)(((double)E[i9].d - r2) / ((double)E[i9].d - E[i9 + 1].d));
                            Px = E[i9].dx * (double)t + E[i9].x;
                            Py = E[i9].dy * (double)t + E[i9].y;
                            f = 1;
                        }
                        i9++;
                    } while (!f && i9 != n);
                    if (f) { BPUSH(0x0a); st = 0; }
                }
            } while (i9 != n);
            if (st != 0) {                     /* @58565-58594 */
                L.x0 = S.x0 + S.dx; L.y0 = S.y0 + S.dy;
                L.dx = (E[n - 1].x + E[n - 1].dx) - L.x0;
                L.dy = (E[n - 1].y + E[n - 1].dy) - L.y0;
                if (0.0008L < fabsl((long double)L.dx) || 0.0008L < fabsl((long double)L.dy)) {
                    line_axis(&L);
                    line_isect(&L, &O, &Px, &Py);
                    BPUSH(0x0a);
                }
            }
            if (!grow((void **)&B, &Bcap, n704 + 2, sizeof *B)) { rc = EG_OFF_ERR_NOMEM; goto done; }
            if (a721 == 0) {                   /* @58595-58609 */
                for (int q = 0; q < n704; q++) w_var(&c, B[q].tag, B[q].x, B[q].y, B[q].z);
            } else {                           /* @58610-58812 */
                N.x0 = B[0].x; N.y0 = B[0].y;
                if (n6f8 > 0) i9 = 1;
                int t723 = 0;
                int t722 = (t724 == 1);
                for (int cnt = n6f8; cnt > 0; cnt--) {
                    int ok;
                    if (!t722) {               /* an up-crossing of level 1 */
                        L.x0 = Ae0[i718].x; L.y0 = Ae0[i718].y;
                        line_to(&L, Ae4[i718].x, Ae4[i718].y);
                        do {
                            M = N;
                            N.x0 = B[i9].x; N.y0 = B[i9].y;
                            line_to(&M, N.x0, N.y0);
                            if (t723 == 0)
                                w_var(&c, B[i9 - 1].tag, B[i9 - 1].x, B[i9 - 1].y, B[i9 - 1].z);
                            ok = line_isect(&M, &L, &Px, &Py);
                            if (ok) ok = in_extent(Px, Py, &M);
                            if (!ok) {
                                i9++;
                                if (n704 < i9) { ok = 1; i9 = n704; }
                            } else {
                                w_var(&c, 0x0a, Px, Py, z933);
                                Ae4[i718].x = Px; Ae4[i718].y = Py;
                                i718++;
                            }
                            t723 = 0;
                        } while (!ok);
                        t722 = 1;
                        N = M;
                    } else {                   /* a down-crossing of level 1 */
                        L.x0 = Ad8[i714].x; L.y0 = Ad8[i714].y;
                        line_to(&L, Adc[i714].x, Adc[i714].y);
                        do {
                            M = N;
                            N.x0 = B[i9].x; N.y0 = B[i9].y;
                            line_to(&M, N.x0, N.y0);
                            ok = line_isect(&M, &L, &Px, &Py);
                            if (ok) ok = in_extent(Px, Py, &M);
                            if (!ok) {
                                i9++;
                                if (n704 < i9) { ok = 1; i9 = n704; }
                            } else {
                                w_var(&c, 0x0b, Px, Py, z933);
                                Adc[i714].x = Px; Adc[i714].y = Py;
                                i714++;
                            }
                        } while (!ok);
                        t722 = 0;
                        N = M;
                        t723 = 1;
                    }
                }
                if (!t722 && n6f8 > 0 && i9 <= n704 - 1)       /* @58800 */
                    for (int q = i9; q < n704; q++) w_var(&c, B[q].tag, B[q].x, B[q].y, B[q].z);
            }
#undef BPUSH
        }
        if (c.nomem) { rc = EG_OFF_ERR_NOMEM; goto done; }
    }

    if (c.var) {                               /* @58827-58888: pair down/up crossings */
        int n710 = n70c;
        for (int q = 0; q < n708; q++) {
            int j;
            for (j = 0; j < n710; j++) {
                double dx = Ad8[q].x - Ae0[j].x, dy = Ad8[q].y - Ae0[j].y;
                double d = sqrt(dx * dx + dy * dy);
                if (d < (double)c.var_tol || d == (double)c.var_tol) break;
            }
            if (j < n710) {
                w_var(&c, 0x0b, Ae4[j].x, Ae4[j].y, z933);
                w_var(&c, 0x0a, Adc[q].x, Adc[q].y, z933);
                Ae0[j] = Ae0[n710 - 1];
                Ae4[j] = Ae4[n710 - 1];
                n710--;
            }
        }
        if (c.nomem) rc = EG_OFF_ERR_NOMEM;
    }
done:
    free(E); free(B); free(Ad8); free(Adc); free(Ae0); free(Ae4);
    return rc;
}

/* ------------------------------------------------------------------ */
/* FUN_00503ed4 state */
typedef struct { int start, end; unsigned char state; } Piece;  /* 9-byte table local_8 */

typedef struct {
    const EgRecs *in;
    EgRecs *out;
    Piece *pc;
    float *sx, *sy;       /* local_14: piece start points */
    float *ex, *ey;       /* local_10: piece end points   */
    int max;              /* local_2dc[0]: index of the last piece */
    float tol;            /* [ebp+8] = dist */
    int closed;           /* FUN_00503e5c [ebp-6] */
    int nomem;
} Join;

/* FUN_00503968 @ 58994: copy piece i to the output (incl = with its 0x0b
 * record), z := 0, mark consumed (0xff). */
static void copy_piece(Join *j, int i, int incl)
{
    Piece *p = &j->pc[i & 0xffff];
    int from = incl ? p->start : p->start + 1;
    int cnt = incl ? p->end - p->start + 1 : p->end - p->start;
    for (int q = 0; q < cnt; q++) {
        const EgRec *r = &j->in->r[from + q];
        if (!recs_push(j->out, r->tag, r->x, r->y, 0.0)) { j->nomem = 1; break; }
    }
    p->state = 0xff;
}

static float fdist(float ax, float ay, float bx, float by)
{
    double dx = (double)ax - bx, dy = (double)ay - by;
    return (float)sqrt(dx * dx + dy * dy);
}

/* FUN_00503a48 @ 59040 (nested in FUN_00503e5c). m = 0: trial walk that only
 * marks pieces (state 1); m = 1: write the chain. */
static void chain(Join *j, int i, int m)
{
    float Sx = j->sx[i], Sy = j->sy[i];
    float Cx = j->ex[i], Cy = j->ey[i];
    if (m) copy_piece(j, i, 1);
    for (;;) {
        float best = fdist(Cx, Cy, Sx, Sy);                     /* @59066-59070 */
        int bi = i;
        if (i < j->max) {
            for (int q = i + 1; q <= j->max; q++) {
                if (j->pc[q].state != 0) continue;
                float d = fdist(Cx, Cy, j->sx[q], j->sy[q]);
                if (d < best) { bi = q; best = d; if (best == 0.0f) break; }
            }
        }
        if (m || best < j->tol) {                              /* @59096 */
            if (bi == i) {
                if (0.0f < best) {                             /* closing record */
                    if (!m) j->pc[bi].state = 1;
                    else if (!recs_push(j->out, 0x0a, Sx, Sy, 0.0)) j->nomem = 1;
                }
                bi = j->max;
                j->closed = 1;
            } else {
                Cx = j->ex[bi]; Cy = j->ey[bi];
                if (!m) j->pc[bi].state = 1;
                else copy_piece(j, bi, 0);
                bi = i;
            }
        } else {
            bi = j->max;
        }
        if (bi == j->max) {                                     /* @59144 */
            if (!m)
                for (int q = i + 1; q <= j->max; q++)
                    if (j->pc[q].state == 1) j->pc[q].state = 0;
            return;
        }
    }
}

/* FUN_00503e5c @ 59165 (dis 503e5c-503ed1) */
static void join_pass(Join *j, int mode)
{
    for (int i = 0; i <= j->max; i++) {
        if (j->pc[i].state != 0) continue;
        if (mode) chain(j, i, 1);
        else {
            j->closed = 0;
            chain(j, i, 0);
            if (j->closed) chain(j, i, 1);
        }
    }
}

/* FUN_00503ed4 @ 59202 (+ FUN_00503784 @ 58906 piece scan) */
int eg_offset_join(const EgRecs *in, EgRecs *out, float tol)
{
    Join j;
    memset(&j, 0, sizeof j);
    j.in = in; j.out = out; j.tol = tol;
    out->n = 0;
    if (in->n < 4) return EG_OFF_NODATA;                       /* @58931 */
    int cap = in->n + 1;
    j.pc = calloc((size_t)cap, sizeof *j.pc);
    j.sx = calloc((size_t)cap, sizeof(float)); j.sy = calloc((size_t)cap, sizeof(float));
    j.ex = calloc((size_t)cap, sizeof(float)); j.ey = calloc((size_t)cap, sizeof(float));
    int rc = EG_OFF_OK;
    if (!j.pc || !j.sx || !j.sy || !j.ex || !j.ey) { rc = EG_OFF_ERR_NOMEM; goto out; }

    /* FUN_00503784: split into pieces at 0x0b records */
    {
        int cnt = 0, seen = 0;
        float lx = 0.0f, ly = 0.0f;                             /* local_14/local_18 */
        for (int q = 0; q < in->n; q++) {
            const EgRec *r = &in->r[q];
            if (r->tag == 0x0b) {
                if (seen) {
                    j.pc[cnt].end = q - 1;
                    if (j.pc[cnt].start < j.pc[cnt].end) {    /* >= 2 records */
                        j.pc[cnt].state = 0;
                        j.ex[cnt] = lx; j.ey[cnt] = ly;
                        cnt++;
                        if (0xffff < cnt) { rc = EG_OFF_PIECES; goto out; }
                    }
                } else seen = 1;
                j.pc[cnt].start = q;
                j.sx[cnt] = (float)r->x; j.sy[cnt] = (float)r->y;
            } else {
                lx = (float)r->x; ly = (float)r->y;
            }
        }
        j.pc[cnt].end = in->n - 1;                              /* @58980 */
        j.pc[cnt].state = 0;
        j.ex[cnt] = lx; j.ey[cnt] = ly;
        j.max = cnt;
    }

    /* pass 0: pieces already closed within 0.001 (DAT_00504148) @59275 */
    for (int i = 0; i <= j.max; i++)
        if (fabsl((long double)j.sx[i] - j.ex[i]) < 0.001L &&
            fabsl((long double)j.sy[i] - j.ey[i]) < 0.001L)
            copy_piece(&j, i, 1);
    join_pass(&j, 0);                                           /* @59293 */
    join_pass(&j, 1);                                           /* @59294 */
    if (j.nomem) { rc = EG_OFF_ERR_NOMEM; goto out; }
    if (out->n < 4) rc = EG_OFF_NODATA;                         /* @59297 */
    for (int i = 0; i <= j.max; i++)                            /* @59300 */
        if (j.pc[i].state != 0xff) { rc = EG_OFF_RETRY; break; }
out:
    free(j.pc); free(j.sx); free(j.sy); free(j.ex); free(j.ey);
    return rc;
}

/* FUN_0050443c @ 59456 (dis 504467-50449a) */
int eg_offset_443c(const EgOffsetPre *pre, EgRecs *out, int side_flag,
                   float dist, float W)
{
    EgOffsetParams k = { side_flag, 1 };
    EgRecs ar2 = { NULL, 0, 0 };                                /* "VekAR2.TMP" */
    int rc = eg_offset_core(pre, &ar2, NULL, 1, 0.0f, dist, W, &k);
    if (rc == EG_OFF_OK) rc = eg_offset_join(&ar2, out, dist);
    eg_recs_free(&ar2);
    return rc;
}

/* FUN_0051001c @ 68719 (dis 510065-5100f9) */
int eg_offset_retry(const EgOffsetPre *pre, EgRecs *out, int side_flag,
                    float dist, float W)
{
    float step = (float)(((double)W - dist) / 2.0);              /* local_c */
    if (0.01L < (long double)step) step = 0.01f;                /* 0x3c23d70a */
    int tries = 0;                                              /* local_d */
    for (;;) {
        int rc = eg_offset_443c(pre, out, side_flag, dist, W);
        if (rc != EG_OFF_RETRY) return rc;
        dist = (float)((double)dist + step);
        if (0.999L * W < (long double)dist) {                   /* DAT_0051011c */
            dist = (float)(0.999L * W);
            if (tries < 4) tries = 4;
        }
        tries++;
        if (5 < tries) return EG_OFF_GIVEUP;
    }
}

#undef Core
#undef Elem
#undef Join
#undef Line
#undef Piece
#undef Pt
#undef chain
#undef clamp_to_seg
#undef copy_piece
#undef eg_recs_free
#undef eg_recs_push
#undef eg_recs_push_rec
#undef fdist
#undef grow
#undef in_extent
#undef join_pass
#undef k_tol_table
#undef line_axis
#undef line_isect
#undef line_to
#undef normal_off
#undef recs_push
#undef w_main
#undef w_var
#undef BPUSH

/* ================================================================================================================== */
/* the offset precomputation FUN_005041a4 -> FUN_00501550 (formats/engrave_engine.md 3)
   (port of eg_pre.c, decoded from EStamp7.exe; renamed file-scope names: pre_*) */
#define G pre_G
#define NL pre_NL
#define PRec pre_PRec
#define Pt pre_Pt
#define band pre_band
#define bbox1 pre_bbox1
#define build_grid pre_build_grid
#define ccap_reserve pre_ccap_reserve
#define cell_add pre_cell_add
#define cell_gather pre_cell_gather
#define cell_index pre_cell_index
#define eg_recs_free pre_eg_recs_free
#define eg_recs_push pre_eg_recs_push
#define eg_recs_push_rec pre_eg_recs_push_rec
#define emit pre_emit
#define envelope pre_envelope
#define fbdec pre_fbdec
#define fbe68 pre_fbe68
#define fbe84 pre_fbe84
#define fbf40 pre_fbf40
#define fbfdc pre_fbfdc
#define fc060 pre_fc060
#define fc098 pre_fc098
#define fc190 pre_fc190
#define fc2a4 pre_fc2a4
#define fc7a0 pre_fc7a0
#define fc890 pre_fc890
#define fcca0 pre_fcca0
#define fcda4 pre_fcda4
#define fcec0 pre_fcec0
#define fd07c pre_fd07c
#define fd1d0 pre_fd1d0
#define fd3a4 pre_fd3a4
#define fe17c pre_fe17c
#define fe214 pre_fe214
#define feaa0 pre_feaa0
#define feb6c pre_feb6c
#define feda8 pre_feda8
#define ff44c pre_ff44c
#define ff658 pre_ff658
#define ff874 pre_ff874
#define find_se pre_find_se
#define gcoord pre_gcoord
#define grow pre_grow
#define near1 pre_near1
#define nl_add pre_nl_add
#define nl_alloc pre_nl_alloc
#define prepare pre_prepare
#define process_contour pre_process_contour
#define push5 pre_push5
#define put6 pre_put6
#define raster pre_raster
#define recs_push pre_recs_push
#define segment pre_segment
#define start_edge pre_start_edge
#define trace_step pre_trace_step
#define wk_append pre_wk_append
#define wk_reserve pre_wk_reserve
/* eg_pre.h - port of the EStamp7 offset PRECOMPUTATION ("Vorberechnung"):
 *   FUN_005041a4 -> FUN_00501550 and its subtree (decompile p_4b0000.c).
 *
 * It reads the prepared contours (VekAR1.TMP, 13-byte LG1 records: tag 0x0b
 * start / 0x0a line-to, x, y float32) and writes the three files that the
 * 2D offset core FUN_00501ec4 (../mod_offset/eg_offset.c) consumes:
 *   VekAR7.TMP  contours after cleanup + corner arcs (25-byte recs, z = 0),
 *               closing segment implicit, trailing 0x0b sentinel;
 *   VekAR6.TMP  per segment the envelope polyline (x, y, z = d), d = height
 *               above the segment line / W;
 *   VekAR5.TMP  int32 cumulative sample counts (one entry per segment + 1).
 * See NOTES.md in this directory for the decoded algorithm and citations.
 */


typedef struct {
    EgRecs ar7;      /* VekAR7.TMP */
    EgRecs ar6;      /* VekAR6.TMP (z = d as float32 widened to double) */
    int *ar5;        /* VekAR5.TMP */
    int n5;
} EgPreOut;

/* engine error codes (*(int*)PTR_DAT_0052f230) set inside this subtree */
#define EG_PRE_OK            0
#define EG_PRE_NODATA        100  /* < 4 input records (FUN_004fd510 @54036) */
#define EG_PRE_TOOMANYPTS    2    /* contour > 0x7ffd points (54076, 53330) */
#define EG_PRE_TOOMANYCONT   3    /* > 19999 contours (FUN_004fcb24 @53647) */
#define EG_PRE_CANDLIST      6    /* candidate list > 0x1fff (FUN_004fe33c @54816) */
#define EG_PRE_LLIST         7    /* edge list > 0x7ffc (FUN_004ff874 @55893) */
#define EG_PRE_TRACE         8    /* trace >= 1999 steps (FUN_00500cac @57084/57121) */
#define EG_PRE_TRACE2        10   /* merged trace too long (57213) */
#define EG_PRE_NOSTART       0xd  /* start/end edge index past list (56904/56958) */
/* port-only codes */
#define EG_PRE_ERR_NOTDECODED (-2001) /* FUN_004fab4c point reduction would run */
#define EG_PRE_ERR_STALE      (-2002) /* original would read a stale/garbage value */
#define EG_PRE_ERR_NOMEM      (-2003)

/* FUN_005041a4(name, side) with the three stack floats of the call at
 * 528e2c-528e5b:
 *   side     param_2 (the caller passes parent[-0x58]); the envelope is built
 *            on normal side (side ^ 1) of FUN_004fbfdc (0 = right of travel).
 *            Use the same value as param_3 of FUN_0051001c / eg_offset_*.
 *   W        precompute width (FUN_00504268, parent[-0x80]), float32.
 *   abr_rad  KAbrundungswinkel (block 7 +4) * 0.017453292519943295, float32:
 *            corner-arc step angle.
 *   A        0.62831855f at that call site (36 deg): concave corner class.
 * in: VekAR1 records (tag 0x0b / 0x0a, x, y; other tags ignored); contours
 * must be explicitly closed (last point == first point) as in VekAR1.
 * Returns 0 or an error code above.  out is always initialised. */
int eg_offset_precompute(const EgRecs *in, int side, float W, float abr_rad,
                         float A, EgPreOut *out);

void eg_pre_free(EgPreOut *o);

static inline void eg_pre_view(const EgPreOut *o, EgOffsetPre *p)
{
    p->ar7 = &o->ar7; p->ar6 = &o->ar6; p->ar5 = o->ar5; p->n5 = o->n5;
}


/* eg_pre.c - port of the EStamp7 offset precomputation FUN_00501550.
 * Citations "@N" are line numbers of decomp/engrave/est/p_4b0000.c,
 * "dis XXXXXX" are addresses of the patched exe (dis.py).  See NOTES.md.
 *
 * Precision: x87 extended intermediates are modelled with long double where
 * the original keeps the value on the FPU stack, values the original stores
 * as float32 are rounded with (float), stored doubles with (double).
 */

#define PI_L   3.141592653589793238L
#define PI2_L  6.283185307179586477L
#define PIH_L  1.570796326794896619L

/* 0x2e-byte segment record (layout from FUN_004fbdec/4fbe68/4fc098/4fe17c):
 * +0 tag, +1 axis flag (1: |dx| <= |dy|), +2 id (i32), +6 x, +0xe y,
 * +0x16 dx, +0x1e dy (doubles), +0x26 slope (f32), +0x2a angle (f32) */
typedef struct {
    unsigned char tag, flag;
    int id;
    double x, y, dx, dy;
    float slope, ang;
} PRec;

typedef struct { double x, y; } Pt;

typedef struct {               /* nearest-intersection list (ffc3c/5001d8/500770) */
    int n;
    int *id;
    Pt *p;
    float *d;
    float best;
} NL;

typedef struct {
    int sd;            /* DAT_005359c0 = side ^ 1 (@57430) */
    float W, abr, A;
    int err;
    /* contour preparation */
    PRec *wk; int wn, wcap;
    int nc, ccap;
    PRec **C, **O;     /* DAT_00535968 / DAT_0053596c tables */
    int *cn, *start;   /* records per contour, cumulative start (+2 + i*4) */
    int total;         /* DAT_00535974 */
    double minx, miny, maxx, maxy;   /* DAT_0053598c.. / DAT_0053597c.. */
    EgRecs *ar7;
    /* grid (FUN_004fde5c) */
    float cell;        /* DAT_00535964 */
    int nx, ny;        /* DAT_005359bc / DAT_005359be */
    int **cl, *cc, *clcap;
    /* envelope (FUN_005013cc) */
    PRec *L; int Ln, Lcap;          /* PTR_DAT_0052f354 list */
    int *cand; int ncand, candcap;  /* candidate ids */
    int candfirst;                  /* DAT_005359c1 */
    int ci, cj;
    float N;                        /* 5013cc [ebp-0xfdc] */
    int Sid[2001], Eid[2001];
    Pt S[2001], E[2001];
    int first; Pt p0; float d0;     /* local_1d, local_34, local_38 */
    EgRecs *ar6;
    int *ar5, n5, cap5;
    int samples;                    /* local_1c */
    NL nl;
} G;

/* ------------------------------------------------------------------ util */
static int grow(void **p, int *cap, int need, size_t sz)
{
    if (need <= *cap) return 0;
    int nc = *cap ? *cap : 16;
    while (nc < need) nc *= 2;
    void *q = realloc(*p, (size_t)nc * sz);
    if (!q) return -1;
    *p = q; *cap = nc;
    return 0;
}

static int recs_push(EgRecs *a, int tag, double x, double y, double z)
{
    if (grow((void **)&a->r, &a->cap, a->n + 1, sizeof *a->r)) return -1;
    a->r[a->n].tag = tag; a->r[a->n].x = x; a->r[a->n].y = y; a->r[a->n].z = z;
    a->n++;
    return 0;
}

/* ------------------------------------------------- geometric primitives */
/* FUN_004fbdec @52900 */
static void fbdec(PRec *r)
{
    if (fabs(r->dx) <= fabs(r->dy)) {
        r->flag = 1;
        r->slope = r->dy == 0.0 ? 0.0f : (float)((long double)r->dx / r->dy);
    } else {
        r->flag = 0;
        r->slope = r->dx == 0.0 ? 0.0f : (float)((long double)r->dy / r->dx);
    }
}
/* FUN_004fbe68 @52929: a.d = b.p - a.p, then fbdec */
static void fbe68(PRec *a, double bx, double by)
{
    a->dx = bx - a->x; a->dy = by - a->y; fbdec(a);
}
/* FUN_004fe17c @54674: same, but only the axis flag (slope left stale) */
static void fe17c(PRec *a, double bx, double by)
{
    a->dx = bx - a->x; a->dy = by - a->y;
    a->flag = fabs(a->dx) <= fabs(a->dy);
}
/* FUN_004fc098 @53090 / dis 4fc098: direction angle in [0, 2pi) from slope */
static void fc098(PRec *r)
{
    long double at = atanl((long double)r->slope);
    if (r->flag) {
        if (r->dy < 0.0) r->ang = (float)(4.71238898038469L - at);
        else r->ang = (float)(PIH_L - at);
    } else if (r->dx < 0.0) r->ang = (float)(at + PI_L);
    else if (r->dy < 0.0) r->ang = (float)(at + PI2_L);
    else r->ang = (float)at;
}
/* FUN_004fc190 dis 4fc190: push a, push b -> (float)(b - a) wrapped to [-pi, pi] */
static float fc190(float a, float b)
{
    float d = (float)((long double)b - a);
    if (!(PI_L >= fabsl(d))) {
        if (d < 0.0f) d = (float)(PI2_L + d);
        else d = (float)(d - PI2_L);
    }
    return d;
}
/* FUN_004fbfdc @53039: normal of length len; side 0 = (dy,-dx), 1 = (-dy,dx) */
static void fbfdc(const PRec *r, int side, double len, double *nx, double *ny)
{
    long double s = len / sqrtl((long double)r->dx * r->dx + (long double)r->dy * r->dy);
    if (side == 0) { *nx = (double)(r->dy * s); *ny = (double)(-r->dx * s); }
    else { *nx = (double)(-r->dy * s); *ny = (double)(r->dx * s); }
}
/* FUN_004fc060 @53071: shift the start point by the normal */
static void fc060(PRec *r, int side, double len)
{
    double nx, ny;
    fbfdc(r, side, len, &nx, &ny);
    r->x += nx; r->y += ny;
}
/* FUN_004fbe84 @52945: line-line intersection, |det| < 1e-19 -> det = 1e-19 */
static Pt fbe84(const PRec *a, const PRec *b)
{
    long double det = (long double)a->dx * b->dy - (long double)a->dy * b->dx;
    if (fabsl(det) < 1e-19L) det = 1e-19L;
    double t = (double)((((long double)b->x - a->x) * b->dy - ((long double)b->y - a->y) * b->dx) / det);
    Pt p; p.x = (double)((long double)t * a->dx + a->x); p.y = (double)((long double)t * a->dy + a->y);
    return p;
}
/* FUN_004fbf40 @52989: same, det == 0 -> not ok (output untouched) */
static int fbf40(const PRec *a, const PRec *b, Pt *out)
{
    long double det = (long double)a->dx * b->dy - (long double)a->dy * b->dx;
    if ((double)det == 0.0) return 0;
    double t = (double)((((long double)b->x - a->x) * b->dy - ((long double)b->y - a->y) * b->dx) / det);
    out->x = (double)((long double)t * a->dx + a->x);
    out->y = (double)((long double)t * a->dy + a->y);
    return 1;
}
/* FUN_004fe214 @54723: point inside the segment extent along its dominant axis, slack 0.0004 */
static int fe214(Pt p, const PRec *r)
{
    const long double e = 0.0004L;
    if (r->flag == 0) {
        if (r->dx >= 0.0) return !((p.x <= r->x - e) || (e + r->x + r->dx < p.x));
        return !((e + r->x <= p.x) || (p.x <= (r->x + (long double)r->dx) - e));
    }
    if (r->dy >= 0.0) return !((p.y <= r->y - e) || (e + r->y + r->dy < p.y));
    return !((e + r->y <= p.y) || (p.y <= (r->y + (long double)r->dy) - e));
}

/* ---------------------------------------------- contour preparation */
static int wk_reserve(G *g, int n) { return grow((void **)&g->wk, &g->wcap, n + 1, sizeof(PRec)); }

/* FUN_004fd07c dis 4fd07c (all args float32) */
static int fd07c(float a0x, float a0y, float a1x, float a1y, float b0x, float b0y,
                 float b1x, float b1y, float *px, float *py)
{
    double ax = (double)((long double)a1x - a0x), ay = (double)((long double)a1y - a0y);
    double mbx = (double)((long double)b0x - b1x), mby = (double)((long double)b0y - b1y);
    double det = (double)((long double)ay * mbx - (long double)ax * mby);
    if (det == 0.0) {
        *px = (float)(((long double)a1x + b0x) / 2.0L);
        *py = (float)(((long double)a1y + b0y) / 2.0L);
        return 1;
    }
    double t = (double)((((long double)b0y - a0y) * mbx - ((long double)b0x - a0x) * mby) / det);
    *px = (float)((long double)t * ax + a0x);
    *py = (float)((long double)t * ay + a0y);
    double u = (double)((((long double)b0x - a0x) * ay - ((long double)b0y - a0y) * ax) / det);
    return t > 0.0 && t <= 1.0 && u > 0.0 && u <= 1.0;
}

static void wk_append(G *g, double x, double y)   /* FUN_004fd038 dis 4fd038 */
{
    g->wk[g->wn].x = x; g->wk[g->wn].y = y; g->wn++;
}

/* FUN_004fd1d0 dis 4fd1d0: one vertex of the 0.0002 shifted polygon */
static void fd1d0(G *g, float off, float lim, float prx, float pry, float cx, float cy,
                  float nx_, float ny_)
{
    double d1x = (double)((long double)cx - prx), d1y = (double)((long double)cy - pry);
    double l1 = (double)sqrtl((long double)d1x * d1x + (long double)d1y * d1y);
    double s1x = (double)((-(long double)off * d1y) / l1), s1y = (double)(((long double)off * d1x) / l1);
    double d2x = (double)((long double)nx_ - cx), d2y = (double)((long double)ny_ - cy);
    double l2 = (double)sqrtl((long double)d2x * d2x + (long double)d2y * d2y);
    double s2x = (double)((-(long double)off * d2y) / l2), s2y = (double)(((long double)off * d2x) / l2);
    float cpx = (float)(cx + (long double)s1x), cpy = (float)(cy + (long double)s1y);
    float px, py;
    int ok = fd07c((float)(prx + (long double)s1x), (float)(pry + (long double)s1y), cpx, cpy,
                   (float)(cx + (long double)s2x), (float)(cy + (long double)s2y),
                   (float)(nx_ + (long double)s2x), (float)(ny_ + (long double)s2y), &px, &py);
    if (ok) { wk_append(g, px, py); return; }
    long double ex = (long double)px - cpx, ey = (long double)py - cpy;
    if (sqrtl(ex * ex + ey * ey) < lim) { wk_append(g, px, py); return; }
    wk_append(g, (float)(((long double)cpx + cx + s2x) / 2.0L), (float)(((long double)cpy + cy + s2y) / 2.0L));
}

/* FUN_004fd3a4 dis 4fd3a4: shift the closed polygon 0.0002 away from the offset side */
static int fd3a4(G *g)
{
    int n = g->wn;
    PRec *T = malloc((size_t)n * sizeof *T);
    if (!T) return EG_PRE_ERR_NOMEM;
    memcpy(T, g->wk, (size_t)n * sizeof *T);
    g->wn = 0;
    float off = g->sd ? -0.0002f : 0.0002f;            /* 0xb951b717 / 0x3951b717 */
    float lim = 0.00022f;                                /* 0x3966afcd */
#define F(k) (float)T[k].x, (float)T[k].y
    fd1d0(g, off, lim, F(n - 2), F(0), F(1));
    for (int i = 1; i <= n - 2; i++) fd1d0(g, off, lim, F(i - 1), F(i), F(i + 1));
    fd1d0(g, off, lim, F(n - 2), F(0), F(1));
#undef F
    free(T);
    return 0;
}

/* FUN_004fcda4 dis 4fcda4: 1 when the turn at B is larger than ~40 deg */
static int fcda4(const PRec *A, const PRec *B, const PRec *Cc)
{
    float ux = (float)(B->x - A->x), uy = (float)(B->y - A->y);
    float vx = (float)(Cc->x - B->x), vy = (float)(Cc->y - B->y);
    float lu = (float)sqrtl((long double)ux * ux + (long double)uy * uy);
    float lv = (float)sqrtl((long double)vx * vx + (long double)vy * vy);
    if (!(lu > 0.0f) || !(lv > 0.0f)) return 0;
    long double sx = (long double)ux / lu + (long double)vx / lv;
    long double sy = (long double)uy / lu + (long double)vy / lv;
    float s = (float)(sx * sx + sy * sy);
    return 3.532L > s;
}

/* FUN_004fcec0 dis 4fcec0: nudge sharp vertices by (c, c) */
static void fcec0(G *g)
{
    PRec *w = g->wk; int n = g->wn;
    if (n < 2) return;
    const long double inc = 0.00042424168043941895L, wrap = 0.00155555L;
    double c = 0.00042424168043941895;                   /* 0x3f3bcd9824a16001 */
    if (fcda4(&w[n - 2], &w[0], &w[1])) {
        w[0].x += c; w[0].y += c; w[n - 1].x += c; w[n - 1].y += c;
        c = (double)(inc + c);
    }
    for (int i = 1; i <= n - 2; i++) {
        if (fcda4(&w[i - 1], &w[i], &w[i + 1])) {
            w[i].x += c; w[i].y += c;
            c = (double)(inc + c);
            if (!(wrap > c)) c = (double)(c - wrap);
        }
    }
}

/* FUN_004fc2a4 dis 4fc2a4-4fc688: corner arcs (convex) and vertex tags */
static int fc2a4(G *g, double a)
{
    PRec *w;
    int n = g->wn;
    for (int i = 0; i < n; i++) {
        w = g->wk;
        fbe68(&w[i], w[(i + 1) % n].x, w[(i + 1) % n].y);
        fc098(&w[i]);
    }
    float cur = g->wk[n - 1].ang, prev;
    int had = 0;
    PRec L64, L94;
    for (int i = 0; i < g->wn; i++) {
        w = g->wk; n = g->wn;
        prev = cur; cur = w[i].ang;
        float t = fc190(prev, cur);
        if ((t > 0.0f) != (g->sd != 0)) {                      /* 4fc33c xor side */
            w[i].tag = 0;
            if (!(fabsf(t) >= a)) { had = 0; continue; }      /* 4fc358 */
            if (had) L64 = L94;
            else { L64 = w[(n + i - 1) % n]; fc060(&L64, g->sd ^ 1, 0.001); }
            L94 = w[i]; fc060(&L94, g->sd ^ 1, 0.001);
            had = 1;
            Pt Cp = fbe84(&L64, &L94);
            float fx = (float)(Cp.x - w[i].x), fy = (float)(Cp.y - w[i].y);
            float fl = (float)sqrtl((long double)fx * fx + (long double)fy * fy);
            if (0.0017L < fl) {                                /* 4fc46a */
                float s = (float)(0.0017L / fl);
                Cp.x = (double)((long double)(fx * s) + w[i].x);
                Cp.y = (double)((long double)(fy * s) + w[i].y);
            }
            int m = (int)truncl((long double)fabsf(t) / a) + 1;   /* 4fc4b3-4fc4c9 */
            if (n + m >= 0x7ffe) return EG_PRE_TOOMANYPTS;        /* 4fc4d4 */
            if (wk_reserve(g, n + m)) return EG_PRE_ERR_NOMEM;
            w = g->wk;
            memmove(&w[i + 1 + m], &w[i + 1], (size_t)(n - i - 1) * sizeof *w);
            n += m; g->wn = n;
            float step = (float)((long double)t / m);
            float th = g->sd ? (float)(PIH_L + prev) : (float)(prev - PIH_L);
            for (int k = i; k <= i + m; k++) {
                w[k].x = (double)(cosl(th) * 0.001L + Cp.x);
                w[k].y = (double)(sinl(th) * 0.001L + Cp.y);
                w[k].tag = 0;
                th = (float)((long double)th + step);
            }
            for (int k = n + i - 1; k <= n + i + m; k++) {
                fbe68(&w[k % n], w[(k + 1) % n].x, w[(k + 1) % n].y);
                fc098(&w[k % n]);
            }
            i += m;
        } else {
            had = 0;
            if (!(0.01745329L <= fabsf(t))) w[i].tag = 0;      /* 4fc638 */
            else if (fabsf(t) > g->A) w[i].tag = 1;            /* 4fc65b */
            else w[i].tag = 2;
        }
    }
    return 0;
}

/* FUN_004fc7a0 dis 4fc7a0: intersection of two offset lines, t > 0.1, u < 0.9 */
static int fc7a0(const PRec *a, const PRec *b, Pt *P)
{
    long double det = (long double)a->dx * b->dy - (long double)a->dy * b->dx;
    if ((double)det == 0.0) return 0;
    long double ex = (long double)b->x - a->x, ey = (long double)b->y - a->y;
    double t = (double)((ex * b->dy - ey * b->dx) / det);
    if (!(t > 0.1L)) return 0;
    if (0.9L <= (ex * a->dy - ey * a->dx) / det) return 0;
    P->x = (double)((long double)t * a->dx + a->x);
    P->y = (double)((long double)t * a->dy + a->y);
    return 1;
}

static void bbox1(G *g, double x, double y)   /* first loop of FUN_004fc890 */
{
    if (x < g->minx) g->minx = x;
    if (g->maxx < x) g->maxx = x;
    if (y < g->miny) g->miny = y;
    if (g->maxy < y) g->maxy = y;
}

/* FUN_004fc890 @53505: offset polygon at W, miter at tag-0 vertices */
static void fc890(G *g)
{
    PRec *w = g->wk; int n = g->wn;
    for (int i = 0; i < n; i++) { bbox1(g, w[i].x, w[i].y); fc060(&w[i], g->sd, (double)g->W); }
    Pt P;
    if (w[0].tag == 0 && fc7a0(&w[n - 1], &w[0], &P)) {
        w[0].dx = (w[0].dx + w[0].x) - P.x; w[0].dy = (w[0].dy + w[0].y) - P.y;
        w[0].x = P.x; w[0].y = P.y;
    }
    for (int i = 1; i < n; i++) {
        if (w[i].tag == 0 && fc7a0(&w[i - 1], &w[i], &P)) {
            w[i].dx = (w[i].dx + w[i].x) - P.x; w[i].dy = (w[i].dy + w[i].y) - P.y;
            w[i].x = P.x; w[i].y = P.y;
            w[i - 1].dx = P.x - w[i - 1].x; w[i - 1].dy = P.y - w[i - 1].y;
        }
    }
    if (w[0].tag == 0) { w[n - 1].dx = w[0].x - w[n - 1].x; w[n - 1].dy = w[0].y - w[n - 1].y; }
    for (int i = 0; i < n; i++) {
        if (!(g->minx <= w[i].x)) g->minx = w[i].x; else if (g->maxx < w[i].x) g->maxx = w[i].x;
        if (!(g->miny <= w[i].y)) g->miny = w[i].y; else if (g->maxy < w[i].y) g->maxy = w[i].y;
    }
}

static int ccap_reserve(G *g, int need)
{
    if (need <= g->ccap) return 0;
    int nc = g->ccap ? g->ccap * 2 : 64;
    while (nc < need) nc *= 2;
    PRec **C2 = realloc(g->C, (size_t)nc * sizeof *C2);
    if (!C2) return -1;
    g->C = C2;
    PRec **O2 = realloc(g->O, (size_t)nc * sizeof *O2);
    if (!O2) return -1;
    g->O = O2;
    int *cn2 = realloc(g->cn, (size_t)nc * sizeof *cn2);
    if (!cn2) return -1;
    g->cn = cn2;
    int *st2 = realloc(g->start, (size_t)nc * sizeof *st2);
    if (!st2) return -1;
    g->start = st2;
    g->ccap = nc;
    return 0;
}

/* FUN_004fcca0 dis 4fcca0 + fc6c0 + fcb24 */
static int fcca0(G *g)
{
    g->wn--;                                           /* drop the closing duplicate */
    double a = (double)g->abr;                         /* [ebp-8] */
    int rc = fc2a4(g, a);
    if (rc == EG_PRE_TOOMANYPTS) return EG_PRE_ERR_NOTDECODED; /* -> FUN_004fab4c loop */
    if (rc) return rc;
    int n = g->wn;
    if (ccap_reserve(g, g->nc + 2)) return EG_PRE_ERR_NOMEM;
    /* FUN_004fc6c0 @53396 */
    g->start[g->nc] = g->total;
    g->C[g->nc] = malloc((size_t)n * sizeof(PRec));
    g->O[g->nc] = malloc((size_t)n * sizeof(PRec));
    if (!g->C[g->nc] || !g->O[g->nc]) return EG_PRE_ERR_NOMEM;
    memcpy(g->C[g->nc], g->wk, (size_t)n * sizeof(PRec));
    g->cn[g->nc] = n;
    for (int i = 0; i < n; i++)
        if (recs_push(g->ar7, i ? 0x0a : 0x0b, g->wk[i].x, g->wk[i].y, 0.0)) return EG_PRE_ERR_NOMEM;
    fc890(g);                                          /* FUN_004fc890 */
    memcpy(g->O[g->nc], g->wk, (size_t)n * sizeof(PRec));   /* FUN_004fcb24 */
    g->nc++;
    if (19999 < g->nc) return EG_PRE_TOOMANYCONT;
    g->total += n;
    return 0;
}

static int process_contour(G *g)
{
    int rc = fd3a4(g);
    if (rc) return rc;
    fcec0(g);
    return fcca0(g);
}

/* FUN_004fd510 @54015 (via FUN_004fd7dc @54126) */
static int prepare(G *g, const EgRecs *in)
{
    if (in->n < 4) return EG_PRE_NODATA;
    g->minx = g->miny = 1e6; g->maxx = g->maxy = -1e6;   /* @57431-57441 */
    g->wn = 0;
    for (int k = 0; k < in->n; k++) {
        int tag = in->r[k].tag;
        float px = (float)in->r[k].x, py = (float)in->r[k].y;
        if (tag == 0x0a) {
            if (g->wn < 1) continue;   /* original reads record -1 (stale); not reached for valid VekAR1 */
            PRec *l = &g->wk[g->wn - 1];
            float ax = (float)fabsl(l->x - (long double)px), ay = (float)fabsl(l->y - (long double)py);
            if ((0.003L < ax) || !(0.003L >= ay)) {          /* dis 4fd63e-4fd667 */
                if (0.0005L > ax) l->x = (double)(0.001L + l->x);
                else if (0.0005L > ay) l->y = (double)(0.001L + l->y);
                if (wk_reserve(g, g->wn + 1)) return EG_PRE_ERR_NOMEM;
                g->wk[g->wn].x = px; g->wk[g->wn].y = py; g->wn++;
                if (g->wn >= 0x7ffe) return EG_PRE_TOOMANYPTS;
            }
        } else if (tag == 0x0b) {
            if (g->wn > 2) { int rc = process_contour(g); if (rc) return rc; }
            if (wk_reserve(g, 1)) return EG_PRE_ERR_NOMEM;
            memset(&g->wk[0], 0, sizeof g->wk[0]);
            g->wk[0].x = px; g->wk[0].y = py; g->wn = 1;
        }
    }
    if (g->wn > 2) { int rc = process_contour(g); if (rc) return rc; }
    if (ccap_reserve(g, g->nc + 2)) return EG_PRE_ERR_NOMEM;
    g->start[g->nc] = g->total;                       /* @54108 */
    /* sentinel FUN_004fcd88 @53770: tag 0x0b, x/y/z are uninitialised stack bytes */
    if (recs_push(g->ar7, 0x0b, 0.0, 0.0, 0.0)) return EG_PRE_ERR_NOMEM;
    return 0;
}

/* ------------------------------------------------------------- grid */
/* FUN_004fd920 @54209 */
static void gcoord(G *g, double x, double y, double *gx, double *gy, int *cx, int *cy)
{
    *gx = (x - g->minx) / (double)g->cell;
    *gy = (y - g->miny) / (double)g->cell;
    *cx = (short)llrintl(*gx); *cy = (short)llrintl(*gy);
}

typedef void (*cellfn)(G *, int cx, int cy, int id);

/* FUN_004fdb88 @54293 / FUN_004fe534 (same code): conservative raster of a line */
static void raster(G *g, const PRec *r, cellfn f, int id)
{
    double g0x, g0y, g1x, g1y; int ax, ay, bx, by;
    gcoord(g, r->x, r->y, &g0x, &g0y, &ax, &ay);
    gcoord(g, r->x + r->dx, r->y + r->dy, &g1x, &g1y, &bx, &by);
    if (r->flag) {                                   /* dis 4fdbe4: columns */
        float c = (float)(g0x - (long double)r->slope * g0y);
        if (ax > bx) { int t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
        for (int col = ax; col <= bx - 1; col++) {
            int yr = (short)llrintl(((long double)(short)col + 0.5L - c) / r->slope);
            int lo = yr < ay ? yr : ay, hi = yr < ay ? ay : yr;
            for (int y = lo; y <= hi; y++) f(g, col, y, id);
            ay = yr;
        }
        int lo = by < ay ? by : ay, hi = by < ay ? ay : by;
        for (int y = lo; y <= hi; y++) f(g, bx, y, id);
    } else {                                         /* rows */
        float c = (float)(g0y - (long double)r->slope * g0x);
        if (ay > by) { int t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; }
        for (int row = ay; row <= by - 1; row++) {
            int xr = (short)llrintl(((long double)(short)row + 0.5L - c) / r->slope);
            int lo = xr < ax ? xr : ax, hi = xr < ax ? ax : xr;
            for (int x = lo; x <= hi; x++) f(g, x, row, id);
            ax = xr;
        }
        int lo = bx < ax ? bx : ax, hi = bx < ax ? ax : bx;
        for (int x = lo; x <= hi; x++) f(g, x, by, id);
    }
}

static int cell_index(int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= 128 || cy >= 128) return -1;   /* outside the 128x128 short grid */
    return cy * 128 + cx;
}

/* FUN_004fd974 @54223: append id unless it equals the last entry */
static void cell_add(G *g, int cx, int cy, int id)
{
    int k = cell_index(cx, cy);
    if (k < 0) { g->err = EG_PRE_ERR_STALE; return; }
    if (g->cc[k] && g->cl[k][g->cc[k] - 1] == id) return;
    if (grow((void **)&g->cl[k], &g->clcap[k], g->cc[k] + 1, sizeof(int))) { g->err = EG_PRE_ERR_NOMEM; return; }
    g->cl[k][g->cc[k]++] = id;
}

/* FUN_004fe3d8 @54829 + FUN_004fe33c @54792: union of cells holding > 1 id */
static void cell_gather(G *g, int cx, int cy, int id)
{
    (void)id;
    int k = cell_index(cx, cy);
    if (k < 0) { g->err = EG_PRE_ERR_STALE; return; }
    int m = g->cc[k];
    if (m <= 1) return;
    if (grow((void **)&g->cand, &g->candcap, g->ncand + m + 1, sizeof(int))) { g->err = EG_PRE_ERR_NOMEM; return; }
    if (g->candfirst) {
        memcpy(g->cand, g->cl[k], (size_t)m * sizeof(int));
        g->ncand = m; g->candfirst = 0;
        return;
    }
    int i = 0;
    for (int q = 0; q < m; q++) {
        int v = g->cl[k][q];
        while (g->cand[i] < v && i < g->ncand) i++;
        if (i == g->ncand) { g->cand[i] = v; g->ncand++; }
        else if (v < g->cand[i]) {
            memmove(&g->cand[i + 1], &g->cand[i], (size_t)(g->ncand - i) * sizeof(int));
            g->cand[i] = v; g->ncand++; i++;
        }
        if (0x1fff < g->ncand) { g->err = EG_PRE_CANDLIST; return; }
    }
}

/* the four boundary lines of a segment's band: C, O, C end->O end, C start->O start
 * (FUN_004fde5c @54614-54657, FUN_004fe7f4 @55150-55191) */
static void band(G *g, int ci, int cj, cellfn f, int id)
{
    const PRec *Cr = &g->C[ci][cj], *Or = &g->O[ci][cj];
    PRec r1 = *Cr, r2;
    memset(&r2, 0, sizeof r2);
    r1.x = Cr->x + Cr->dx; r1.y = Cr->y + Cr->dy;
    fbe68(&r1, Or->x + Or->dx, Or->y + Or->dy);
    r2 = r1; r2.x = Cr->x; r2.y = Cr->y;
    fbe68(&r2, Or->x, Or->y);
    raster(g, Cr, f, id);
    raster(g, Or, f, id);
    raster(g, &r1, f, id);
    raster(g, &r2, f, id);
}

/* FUN_004fde5c @54516 / dis 4fde5c */
static int build_grid(G *g)
{
    g->cell = (float)(1.1L * g->W);                  /* FUN_004fd7dc @54143 */
    long double wx = g->maxx - g->minx, wy = g->maxy - g->miny;
    g->nx = (short)((int)truncl(wx / g->cell) + 5);
    while (g->nx >= 0x7f) {
        g->cell = (float)((long double)(g->nx + 5) / 128.0L * g->cell);
        g->nx = (short)((int)truncl(wx / g->cell) + 5);
    }
    g->ny = (short)((int)truncl(wy / g->cell) + 5);
    while (g->ny >= 0x7f) {
        g->cell = (float)((long double)(g->ny + 4) / 128.0L * g->cell);
        g->nx = (short)((int)truncl(wx / g->cell) + 5);
        g->ny = (short)((int)truncl(wy / g->cell) + 5);
    }
    g->minx = (double)(g->minx - (long double)g->cell * 2.0f);
    g->miny = (double)(g->miny - (long double)g->cell * 2.0f);
    g->cl = calloc(128 * 128, sizeof(int *));
    g->cc = calloc(128 * 128, sizeof(int));
    g->clcap = calloc(128 * 128, sizeof(int));
    if (!g->cl || !g->cc || !g->clcap) return EG_PRE_ERR_NOMEM;
    for (int ci = 0; ci < g->nc; ci++)
        for (int cj = 0; cj < g->cn[ci]; cj++) {
            band(g, ci, cj, cell_add, g->start[ci] + cj);
            if (g->err) return g->err;
        }
    return 0;
}

/* ------------------------------------------------ envelope: edge list */
/* FUN_004ff44c @55612: clip the bisector line E to the band (C, O) of a segment */
static int ff44c(const PRec *Cr, const PRec *Or, const PRec *Eb, Pt Q, Pt out[3])
{
    PRec se, ss;
    memset(&se, 0, sizeof se); memset(&ss, 0, sizeof ss);
    se.x = Cr->x + Cr->dx; se.y = Cr->y + Cr->dy; fe17c(&se, Or->x + Or->dx, Or->y + Or->dy);
    ss.x = Cr->x; ss.y = Cr->y; fe17c(&ss, Or->x, Or->y);
    int n = 0;
    if (fbf40(Eb, &se, &out[n]) && fe214(out[n], &se)) n++;
    if (fbf40(Eb, &ss, &out[n]) && fe214(out[n], &ss)) n++;
    if (n < 2) {
        out[n] = Q;
        if (fe214(out[n], Or)) n++;
        if (n != 0) {
            if (n == 2 && fabsl((long double)out[0].x - out[1].x) < 0.0001L &&
                fabsl((long double)out[0].y - out[1].y) < 0.0001L) n--;
            if (n == 1) {
                out[1].x = Eb->x; out[1].y = Eb->y;
                if (fe214(out[1], Cr) && (0.0001L < fabsl((long double)out[0].x - out[1].x) ||
                                          0.0001L < fabsl((long double)out[0].y - out[1].y))) n++;
            }
        }
    }
    return n;
}

/* FUN_004feda8 @55338: intersect clip intervals A (own band) and B (other band), orient */
static int feda8(PRec *out, Pt *Q, Pt A[3], Pt B[3], const PRec *cur)
{
    Pt *A0 = &A[0], *A1 = &A[1], *B0 = &B[0], *B1 = &B[1];
#define CLIP(c)                                                                  \
    if (A1->c <= A0->c) {                                                        \
        if (B1->c <= B0->c) {                                                    \
            if (B0->c < A1->c || A0->c < B1->c) return 0;                        \
            if (A1->c < B1->c) *A1 = *B1;                                         \
            if (B0->c < A0->c) *A0 = *B0;                                         \
        } else {                                                                 \
            if (B1->c < A1->c || A0->c < B0->c) return 0;                        \
            if (A1->c < B0->c) *A1 = *B0;                                         \
            if (B1->c < A0->c) *A0 = *B1;                                         \
        }                                                                        \
    } else {                                                                     \
        if (B1->c <= B0->c) {                                                    \
            if (B0->c < A0->c || A1->c < B1->c) return 0;                        \
            if (A0->c < B1->c) *A0 = *B1;                                         \
            if (B0->c < A1->c) *A1 = *B0;                                         \
        } else {                                                                 \
            if (B1->c < A0->c || A1->c < B0->c) return 0;                        \
            if (A0->c < B0->c) *A0 = *B0;                                         \
            if (B1->c < A1->c) *A1 = *B1;                                         \
        }                                                                        \
    }
    if (out->flag == 0) { CLIP(x) } else { CLIP(y) }
#undef CLIP
    int sw;
    if (out->flag == cur->flag) {
        if (out->flag == 0) sw = (A0->x < A1->x) == (cur->dx < 0.0);
        else sw = (A0->y < A1->y) == (cur->dy < 0.0);
    } else if ((fabsf(out->slope) < fabsf(cur->slope)) == (out->flag != 0)) {
        sw = (A0->y < A1->y) == (cur->dy < 0.0);
    } else {
        sw = (A0->x < A1->x) == (cur->dx < 0.0);
    }
    if (sw) { out->x = A1->x; out->y = A1->y; *Q = *A0; }
    else { out->x = A0->x; out->y = A0->y; *Q = *A1; }
    out->dx = Q->x - out->x; out->dy = Q->y - out->y;
    return 1;
}

/* FUN_004feaa0 @55246: E line against the start side and end side of a band */
static void feaa0(const PRec *Cr, const PRec *Or, PRec *Eb, Pt *o3, Pt *o4, int *chk, int o3_is_E)
{
    PRec s;
    memset(&s, 0, sizeof s);
    s.x = Cr->x; s.y = Cr->y; fe17c(&s, Or->x, Or->y);
    Pt p = fbe84(Eb, &s);
    if (o3_is_E) { Eb->x = p.x; Eb->y = p.y; } else *o3 = p;
    if (*chk == 0) *chk = fe214(p, &s);
    s.x = Cr->x + Cr->dx; s.y = Cr->y + Cr->dy; fe17c(&s, Or->x + Or->dx, Or->y + Or->dy);
    *o4 = fbe84(Eb, &s);
}

/* FUN_004feb6c dis 4feb6c + FUN_004fe940 @55198: antiparallel segments -> midline */
static int feb6c(PRec *out, Pt *Q, const PRec *curC, const PRec *curO,
                 const PRec *candC, const PRec *candO)
{
    PRec s;
    memset(&s, 0, sizeof s);
    out->flag = curC->flag;
    s.x = curC->x; s.y = curC->y; fe17c(&s, curO->x, curO->y);
    Pt X1 = fbe84(&s, candC);
    out->x = (double)(((long double)curC->x + X1.x) / 2.0L);
    out->y = (double)(((long double)curC->y + X1.y) / 2.0L);
    s.x = curC->x + curC->dx; s.y = curC->y + curC->dy;
    fe17c(&s, curO->x + curO->dx, curO->y + curO->dy);
    Pt X2 = fbe84(&s, candC);
    Q->x = (double)(((long double)(curC->x + curC->dx) + X2.x) / 2.0L);
    Q->y = (double)(((long double)(curC->y + curC->dy) + X2.y) / 2.0L);
    out->dx = Q->x - out->x; out->dy = Q->y - out->y;
    int chk = 0;
    Pt dummy, R0, R1;
    feaa0(curC, curO, out, &dummy, Q, &chk, 1);
    if (!chk) return 0;
    feaa0(candC, candO, out, &R0, &R1, &chk, 0);
    if (curC->flag) {
        int dl = curC->dy < 0.0;
        if ((R1.y > Q->y) != dl) return 0;
        if ((R0.y < out->y) != dl) return 0;
        if ((R1.y > out->y) != dl) { out->x = R1.x; out->y = R1.y; }
        if ((R0.y < Q->y) != dl) *Q = R0;
    } else {
        int dl = curC->dx < 0.0;
        if ((R1.x > Q->x) != dl) return 0;
        if ((R0.x < out->x) != dl) return 0;
        if ((R1.x > out->x) != dl) { out->x = R1.x; out->y = R1.y; }
        if ((R0.x < Q->x) != dl) *Q = R0;
    }
    out->dx = Q->x - out->x; out->dy = Q->y - out->y;
    return 1;
}

/* FUN_004ff658 @55728 / dis 4ff658: edge of the current cell against segment (ic, jc) */
static int ff658(G *g, int ic, int jc, PRec *out)
{
    const PRec *candC = &g->C[ic][jc], *curC = &g->C[g->ci][g->cj];
    float da = fc190(curC->ang, candC->ang);
    if (0.01745329L > fabsf(da)) return 0;
    if (ic == g->ci) {
        int n = g->cn[g->ci];
        if (jc == g->cj) return 0;
        if ((n + jc - 1) % n == g->cj && candC->tag == 0) return 0;
        if ((jc + 1) % n == g->cj && curC->tag == 0) return 0;
    }
    const PRec *curO = &g->O[g->ci][g->cj], *candO = &g->O[ic][jc];
    Pt Q;
    out->tag = 0;
    if (!(3.1398473245897933L >= fabsf(da)))
        return feb6c(out, &Q, curC, curO, candC, candO);
    Pt P = fbe84(curC, candC);
    out->x = P.x; out->y = P.y;
    Q = fbe84(curO, candO);
    fbe68(out, Q.x, Q.y);
    Pt A[3], B[3];
    if (ff44c(curC, curO, out, Q, A) < 2) return 0;
    if (ff44c(candC, candO, out, Q, B) < 2) return 0;
    return feda8(out, &Q, A, B, curC);
}

/* FUN_004ff874 @55832: build the edge list L of the current segment */
static int ff874(G *g)
{
    g->Ln = 0;
    g->ncand = 0; g->candfirst = 1;                     /* FUN_004fe7f4 @55148-55149 */
    band(g, g->ci, g->cj, cell_gather, 0);
    if (g->err) return g->err;
    PRec out;
    memset(&out, 0, sizeof out);                        /* ff874 local_74: persists */
    for (int q = 0; q < g->ncand; q++) {
        int id = g->cand[q], k;
        for (k = 1; g->start[k] <= id && k < g->nc; k++) {}
        int ic = k - 1, jc = id - g->start[ic];
        if (!ff658(g, ic, jc, &out)) continue;
        fbdec(&out); fc098(&out);
        out.id = id;
        if (grow((void **)&g->L, &g->Lcap, g->Ln + 2, sizeof(PRec))) return EG_PRE_ERR_NOMEM;
        g->L[g->Ln++] = out;
        if (0x7ffc < g->Ln) return EG_PRE_LLIST;
    }
    if (grow((void **)&g->L, &g->Lcap, g->Ln + 2, sizeof(PRec))) return EG_PRE_ERR_NOMEM;
    g->L[g->Ln] = g->O[g->ci][g->cj];
    g->L[g->Ln].id = -1;      /* original: bytes +2..+5 of the O record, never written (NOT DECODED) */
    g->Ln++;
    return 0;
}

/* ------------------------------------------------ envelope: tracing */
/* insertion rule shared by ffc3c / 5001d8 / 500770 (dis 4ffe91-50004c) */
static void nl_add(NL *l, int id, Pt p, float dist, int cap)
{
    const long double e = 0.00033L;
    if (!((long double)l->best + e <= dist)) {
        if (!((long double)l->best - e <= dist)) {
            l->id[0] = id; l->p[0] = p; l->d[0] = dist; l->best = dist; l->n = 1;
        } else if (!(dist >= l->best)) {
            memmove(&l->id[1], &l->id[0], (size_t)l->n * sizeof(int));
            memmove(&l->p[1], &l->p[0], (size_t)l->n * sizeof(Pt));
            memmove(&l->d[1], &l->d[0], (size_t)l->n * sizeof(float));
            l->id[0] = id; l->p[0] = p; l->d[0] = dist; l->best = dist;
            if (l->n < cap) l->n++;
        } else {
            int k = 1;
            while (k < l->n && l->d[k] < dist) k++;
            memmove(&l->id[k + 1], &l->id[k], (size_t)(l->n - k) * sizeof(int));
            memmove(&l->p[k + 1], &l->p[k], (size_t)(l->n - k) * sizeof(Pt));
            memmove(&l->d[k + 1], &l->d[k], (size_t)(l->n - k) * sizeof(float));
            l->id[k] = id; l->p[k] = p; l->d[k] = dist;
            if (l->n < cap) l->n++;
        }
    }
}

static int nl_alloc(G *g)
{
    int need = g->Ln + 2;
    int *id = realloc(g->nl.id, (size_t)need * sizeof(int));
    if (!id) return -1;
    g->nl.id = id;
    Pt *p = realloc(g->nl.p, (size_t)need * sizeof(Pt));
    if (!p) return -1;
    g->nl.p = p;
    float *d = realloc(g->nl.d, (size_t)need * sizeof(float));
    if (!d) return -1;
    g->nl.d = d;
    g->nl.n = 0; g->nl.best = 1e7f;
    return 0;
}

/* FUN_004ffc3c @56085 (S trace, forward) and FUN_005001d8 @56409 (E trace, backward) */
static int trace_step(G *g, int k, int fwd)
{
    int *ids = fwd ? g->Sid : g->Eid;
    Pt *pts = fwd ? g->S : g->E;
    ids[k] = -1;
    int cur = ids[k - 1], prev = k > 1 ? ids[k - 2] : cur;
    if (cur < 0 || cur >= g->Ln) return EG_PRE_ERR_STALE;
    Pt P = pts[k - 1];
    const PRec *R = &g->L[cur];
    float lim = fabsf(fc190(g->N, R->ang));
    if (nl_alloc(g)) return EG_PRE_ERR_NOMEM;
    NL *l = &g->nl;
    const long double e = 0.00022L;
    for (int s = 0; s < g->Ln; s++) {
        if (s == cur || s == prev) continue;
        Pt X;
        int ok = fbf40(R, &g->L[s], &X);
        if (ok) {
            if (R->flag == 0) {
                if (fwd) ok = R->dx < 0.0 ? (X.x < P.x && R->x + R->dx - e < X.x)
                                          : (X.x > P.x && R->x + R->dx + e > X.x);
                else ok = R->dx < 0.0 ? (X.x > P.x && e + R->x > X.x)
                                      : (X.x < P.x && R->x - e < X.x);
            } else {
                if (fwd) ok = R->dy < 0.0 ? (X.y < P.y && R->y + R->dy - e < X.y)
                                          : (X.y > P.y && R->y + R->dy + e > X.y);
                else ok = R->dy < 0.0 ? (X.y > P.y && e + R->y > X.y)
                                      : (X.y < P.y && R->y - e < X.y);
            }
            if (ok) {
                float c = fabsf(fc190(g->N, g->L[s].ang));
                if (fwd ? (c >= lim) : (c <= lim)) ok = 0;
                else ok = fe214(X, &g->L[s]);
            }
        }
        if (ok) {
            float dist = (float)(R->flag ? fabsl((long double)P.y - X.y) : fabsl((long double)P.x - X.x));
            nl_add(l, s, X, dist, 1 << 30);
        }
    }
    if (l->n == 0) {               /* continuation edges sharing the end (fwd) / start point */
        double ex = fwd ? R->x + R->dx : R->x, ey = fwd ? R->y + R->dy : R->y;
        for (int s = 0; s < g->Ln; s++) {
            if (s == cur || s == prev) continue;
            const PRec *T = &g->L[s];
            double tx = fwd ? T->x : T->x + T->dx, ty = fwd ? T->y : T->y + T->dy;
            if (0.00033L > fabsl((long double)ex - tx) && 0.00033L > fabsl((long double)ey - ty)) {
                l->id[l->n] = s;
                l->p[l->n].x = fwd ? T->x : R->x; l->p[l->n].y = fwd ? T->y : R->y;
                l->n++;
            }
        }
    }
    if (l->n > 0) {
        float best = fwd ? 6.2831855f : 0.0f;
        for (int q = l->n - 1; q >= 0; q--) {
            float c = fabsf(fc190(g->N, g->L[l->id[q]].ang));
            if (fwd ? !(c > best) : !(c < best)) { ids[k] = l->id[q]; pts[k] = l->p[q]; best = c; }
        }
    }
    return 0;
}

/* FUN_00500770 @56718: the edge that starts (mode 1) / ends (mode 0) on the vertex line V */
static int start_edge(G *g, int mode, const PRec *V, int *idout)
{
    if (nl_alloc(g)) return EG_PRE_ERR_NOMEM;
    NL *l = &g->nl;
    for (int u = 0; u < g->Ln; u++) {
        Pt X;
        if (!fbf40(V, &g->L[u], &X)) continue;
        const PRec *T = &g->L[u];
        double tx = mode == 0 ? T->x + T->dx : T->x, ty = mode == 0 ? T->y + T->dy : T->y;
        if (fabsl((long double)X.x - tx) < 0.00033L && fabsl((long double)X.y - ty) < 0.00033L) {
            float dist = (float)(V->flag ? fabsl((long double)V->y - X.y) : fabsl((long double)V->x - X.x));
            nl_add(l, u, X, dist, 299);
        }
    }
    if (l->n > 0) {
        float best = mode ? 6.2831855f : 0.0f;
        for (int q = l->n - 1; q >= 0; q--) {
            float c = fabsf(fc190(g->N, g->L[l->id[q]].ang));
            if ((c > best) != (mode != 0)) { *idout = l->id[q]; best = c; }
        }
    }
    return 0;
}

/* FUN_00500a64 @56863 (end, mode 0) / FUN_00500b7c @56913 (start, mode 1) */
static int find_se(G *g, int mode, int *idout)
{
    int n = g->cn[g->ci];
    int j = mode == 0 ? (g->cj + 1) % n : g->cj;
    if (g->C[g->ci][j].tag == 0) {
        PRec V;
        memset(&V, 0, sizeof V);
        V.x = g->C[g->ci][j].x; V.y = g->C[g->ci][j].y;
        fe17c(&V, g->O[g->ci][j].x, g->O[g->ci][j].y);
        return start_edge(g, mode, &V, idout);
    }
    int want = g->start[g->ci] + (mode == 0 ? j : (g->cj + n - 1) % n);
    int k = 0;
    while (k < g->Ln && want != g->L[k].id) k++;
    *idout = k;
    if (k >= g->Ln) return EG_PRE_ERR_STALE;   /* original reads L[Ln] (stale record) */
    return 0;
}

/* FUN_004ffa4c @55948 */
static int put6(G *g, int tag, Pt p, float d)
{
    if (recs_push(g->ar6, tag, p.x, p.y, (double)d)) return EG_PRE_ERR_NOMEM;
    g->samples++;
    return 0;
}

/* FUN_004ffb0c @56000: one envelope sample, d = height / W along the normal */
static int emit(G *g, Pt P)
{
    const PRec *Cr = &g->C[g->ci][g->cj];
    double nx, ny;
    fbfdc(Cr, g->sd, (double)g->W, &nx, &ny);
    PRec R;
    memset(&R, 0, sizeof R);
    R.x = P.x; R.y = P.y; R.dx = nx; R.dy = ny;
    fbdec(&R);
    Pt F = fbe84(&R, Cr);
    float d = R.flag == 0 ? (float)fabsl(((long double)P.x - F.x) / nx)
                          : (float)fabsl(((long double)P.y - F.y) / ny);
    int tag = 0x0a;
    if (g->first) { g->p0 = P; g->d0 = d; tag = 0x0b; g->first = 0; }
    return put6(g, tag, P, d);
}

static int near1(Pt a, Pt b, long double e)
{
    return !(e <= fabsl((long double)a.x - b.x)) && !(e <= fabsl((long double)a.y - b.y));
}

/* FUN_00500cac @56970: envelope polyline of one segment */
static int segment(G *g)
{
    int rc = ff874(g);
    if (rc) return rc;
    const PRec *Cs = g->C[g->ci];
    int n = g->cn[g->ci], j1 = (g->cj + 1) % n;
    if (g->Ln < 2) {
        Pt a = { g->L[0].x, g->L[0].y }, b = { g->L[0].x + g->L[0].dx, g->L[0].y + g->L[0].dy };
        if ((rc = emit(g, a))) return rc;
        return emit(g, b);
    }
    float a = Cs[g->cj].ang;
    if (g->sd) { g->N = (float)((long double)a - PIH_L); if (g->N < 0.0f) g->N = (float)(PI2_L + g->N); }
    else { g->N = (float)(PIH_L + a); if (!(PI2_L > g->N)) g->N = (float)(g->N - PI2_L); }
    if ((rc = find_se(g, 0, &g->Eid[0]))) return rc;
    if (g->Eid[0] < 0 || g->Eid[0] >= g->Ln) return EG_PRE_ERR_STALE;
    { const PRec *e = &g->L[g->Eid[0]]; g->E[0].x = e->x + e->dx; g->E[0].y = e->y + e->dy; }
    if ((rc = trace_step(g, 1, 0))) return rc;
    if ((rc = find_se(g, 1, &g->Sid[0]))) return rc;
    if (g->Sid[0] < 0 || g->Sid[0] >= g->Ln) return EG_PRE_ERR_STALE;
    g->S[0].x = g->L[g->Sid[0]].x; g->S[0].y = g->L[g->Sid[0]].y;

    if (g->Sid[0] == g->Eid[0] && g->Eid[0] != g->Ln - 1) {      /* @57054 */
        if (Cs[g->cj].tag != 2 && (rc = emit(g, g->S[0]))) return rc;
        if (Cs[j1].tag != 2 && (rc = emit(g, g->E[0]))) return rc;
        return 0;
    }
    int m = 0;
    if (g->Eid[1] == -1) {                                        /* @57066 */
        if (g->Sid[0] != g->Eid[0]) {
            int k = 0;
            do {
                m = k; k = m + 1;
                if ((rc = trace_step(g, k, 1))) return rc;
                if (g->Sid[k] == -1) break;
            } while (k < 1999);
            if (0x7ce < k) return EG_PRE_TRACE;
        }
    } else {
        int k = 0, k0;
        do {
            k0 = k; k = k0 + 1;
            if ((rc = trace_step(g, k, 1))) return rc;
            if (g->Sid[k] == -1 || k == 1999) break;
        } while (!near1(g->S[k], g->E[1], 0.0001L));
        m = k;
        if (g->Sid[k] == -1 || 0x7ce < k) {
            int ns = k0;                                          /* iVar9: good S points */
            if (ns < 1) {
                int e = 1, last;
                do {
                    last = e; e = last + 1;
                    if ((rc = trace_step(g, e, 0))) return rc;
                    if (g->Eid[e] == -1) break;
                } while (e != 1999);
                if (e == 1999) return EG_PRE_TRACE;
                m = last;
                for (int t = m; t >= 1; t--) g->S[1 + m - t] = g->E[t];
            } else {
                int e = 1, ne;
                do {
                    ne = e; e = ne + 1;
                    if ((rc = trace_step(g, e, 0))) return rc;
                    if (g->Eid[e] == -1 || 0x7ce < e) break;
                } while (!near1(g->S[1], g->E[e], 0.0003L));
                m = e;
                if (g->Eid[e] == -1 || 0x7ce < e) {
                    if (ns < 2 || ne < 2) {
                        for (int t = ne; t >= 1; t--) g->S[ns + 1 + ne - t] = g->E[t];
                        m = ns + ne;
                    } else {
                        int found = 0, a8 = 2, b10 = 2;
                        do {
                            if (!near1(g->S[a8], g->E[b10], 0.0001L)) {
                                a8++;
                                if (ns < a8) { a8 = 2; b10++; }
                            } else found = 1;
                        } while (!found && b10 <= ne);
                        if (found) {
                            m = a8 + b10 - 1;
                            for (int t = b10 - 1; t > 0; t--) {
                                if (0x7ce < a8 + b10 - t) return EG_PRE_TRACE2;
                                g->S[a8 + b10 - t] = g->E[t];
                            }
                        } else if (ns + ne < 1999) {
                            for (int t = ne; t >= 1; t--) g->S[ns + 1 + ne - t] = g->E[t];
                            m = ns + ne;
                        } else m = 1;
                    }
                } else {
                    for (int t = m; t >= 1; t--) g->S[1 + m - t] = g->E[t];
                }
            }
        }
    }
    if (Cs[g->cj].tag != 2 && (rc = emit(g, g->S[0]))) return rc;
    for (int t = 1; t <= m; t++) if ((rc = emit(g, g->S[t]))) return rc;
    if (Cs[j1].tag != 2 && (rc = emit(g, g->E[0]))) return rc;
    return 0;
}

static int push5(G *g, int v)
{
    if (grow((void **)&g->ar5, &g->cap5, g->n5 + 1, sizeof(int))) return EG_PRE_ERR_NOMEM;
    g->ar5[g->n5++] = v;
    return 0;
}

/* FUN_005013cc @57308 */
static int envelope(G *g)
{
    int rc;
    g->samples = 0;
    g->ci = 0; g->cj = 0;
    memset(g->Sid, 0, sizeof g->Sid); memset(g->Eid, 0, sizeof g->Eid);
    do {
        if ((rc = push5(g, g->samples))) return rc;             /* FUN_004fe1e4 @57357 */
        if (g->cj == 0) g->first = 1;
        if ((rc = segment(g))) return rc;
        g->cj++;
        if (g->cj == g->cn[g->ci]) {
            if ((rc = put6(g, 0x0a, g->p0, g->d0))) return rc;  /* @57372-57374 */
            g->cj = 0; g->ci++;
        }
    } while (g->nc != g->ci);
    if ((rc = put6(g, 0x0b, g->p0, g->d0))) return rc;          /* @57382-57383 */
    return push5(g, g->samples);                                /* @57386 */
}

/* ------------------------------------------------------------- API */
void eg_pre_free(EgPreOut *o)
{
    if (!o) return;
    free(o->ar7.r); free(o->ar6.r); free(o->ar5);
    memset(o, 0, sizeof *o);
}

int eg_offset_precompute(const EgRecs *in, int side, float W, float abr_rad, float A,
                         EgPreOut *out)
{
    memset(out, 0, sizeof *out);
    G *g = calloc(1, sizeof *g);
    if (!g) return EG_PRE_ERR_NOMEM;
    g->sd = (side ^ 1) & 1;
    g->W = W; g->abr = abr_rad; g->A = A;
    g->ar7 = &out->ar7; g->ar6 = &out->ar6;
    int rc = prepare(g, in);
    if (!rc && g->nc == 0) rc = EG_PRE_NODATA;   /* (no contour > 2 points: no envelope possible) */
    if (!rc) rc = build_grid(g);
    if (!rc) rc = envelope(g);
    out->ar5 = g->ar5; out->n5 = g->n5;
    /* cleanup */
    for (int i = 0; i < g->nc; i++) { free(g->C[i]); free(g->O[i]); }
    free(g->C); free(g->O); free(g->cn); free(g->start); free(g->wk);
    if (g->cl) for (int k = 0; k < 128 * 128; k++) free(g->cl[k]);
    free(g->cl); free(g->cc); free(g->clcap);
    free(g->L); free(g->cand);
    free(g->nl.id); free(g->nl.p); free(g->nl.d);
    free(g);
    return rc;
}

#undef G
#undef NL
#undef PRec
#undef Pt
#undef band
#undef bbox1
#undef build_grid
#undef ccap_reserve
#undef cell_add
#undef cell_gather
#undef cell_index
#undef eg_recs_free
#undef eg_recs_push
#undef eg_recs_push_rec
#undef emit
#undef envelope
#undef fbdec
#undef fbe68
#undef fbe84
#undef fbf40
#undef fbfdc
#undef fc060
#undef fc098
#undef fc190
#undef fc2a4
#undef fc7a0
#undef fc890
#undef fcca0
#undef fcda4
#undef fcec0
#undef fd07c
#undef fd1d0
#undef fd3a4
#undef fe17c
#undef fe214
#undef feaa0
#undef feb6c
#undef feda8
#undef ff44c
#undef ff658
#undef ff874
#undef find_se
#undef gcoord
#undef grow
#undef near1
#undef nl_add
#undef nl_alloc
#undef prepare
#undef process_contour
#undef push5
#undef put6
#undef raster
#undef recs_push
#undef segment
#undef start_edge
#undef trace_step
#undef wk_append
#undef wk_reserve
#undef CLIP
#undef F
#undef PI2_L
#undef PIH_L
#undef PI_L

/* ================================================================================================================== */
/* the V-tool 3D corner sharpening "Ausspitzen" FUN_00517594 / FUN_00518bd4 (formats/engrave_engine.md 5)
   (port of eg_sharp.c, decoded from EStamp7.exe; renamed file-scope names: shp_*) */
#define F32 shp_F32
#define GrState shp_GrState
#define SLOT0 shp_SLOT0
#define Slot shp_Slot
#define SpState shp_SpState
#define eg_recs_free shp_eg_recs_free
#define eg_recs_push shp_eg_recs_push
#define eg_recs_push_rec shp_eg_recs_push_rec
#define gr_ang shp_gr_ang
#define gr_close shp_gr_close
#define gr_corner shp_gr_corner
#define gr_isect shp_gr_isect
#define gr_perp shp_gr_perp
#define gr_seg shp_gr_seg
#define gr_tan shp_gr_tan
#define gr_turn shp_gr_turn
#define gr_write shp_gr_write
#define sp_put shp_sp_put
#define sp_write shp_sp_write
/* eg_sharp.h - EStamp7.exe "Ausspitzen" (3D corner sharpening) finishing pass,
 * KorrekturVerf '3' (2D3D) / 'A' (3D only).  Port of:
 *   Spiegel: FUN_00517ac8 (driver) -> FUN_00517594 (core) + FUN_00517204, FUN_0051710c, FUN_005170b8
 *   Grund:   FUN_00518bd4 (core) + FUN_00518960, FUN_00518b48, FUN_00518650, FUN_00518610,
 *            FUN_00518738, FUN_005186b8, FUN_00518754, FUN_0051884c, FUN_005188c4,
 *            FUN_004a1b40, FUN_004a1f04
 * See NOTES.md for the decoded algorithm with citations.
 * Units: whatever the engine works in (mm).  Record z is a POSITIVE depth below the top surface
 * (0 = surface); engrave.dll negates it when it reads ENGTOOLP.LG1.
 */


/* record tags (LG1) */
#define EG_TAG_LINE   0x0a
#define EG_TAG_MOVE   0x0b
#define EG_TAG_POINT  0x0c

/* return codes */
#define EG_SHARP_OK              0
#define EG_SHARP_ERR_NOMEM      -1
#define EG_SHARP_ERR_54         54  /* FUN_00518bd4 sets global error 0x36 (bad tool row) */
#define EG_SHARP_ERR_NOT_DECODED -2 /* input the original handles by corrupting its own frame */

/* If nonzero (default), every value the original stores into a 4-byte float is rounded to float
 * at the same place, so the output can match the original record by record. */
extern int eg_sharp_f32;

/* FUN_00517ac8 arithmetic (no I/O): effective offset L = R+0x18 + offset (the distance the 2D offset
 * engine FUN_005041dc is called with) and ratio = L / (L - (R+0xc + offset)).
 * r18 = tool row +0x18 (effective radius at depth), rc = row +0xc (tip radius), offset = *(float*)
 * finishing allowance passed in by FUN_00528c9c. */
void eg_sharp_spiegel_params(double r18, double rc, double offset, double *L, double *ratio);

/* FUN_00517594: in = contents of VekAR2.TMP written by FUN_005041dc(mode char 0) - records whose z
 * is the normalised distance d/L from the contour (1.0 on the full offset).  Appends to out (the
 * original opens the per-tool output file for append).  only3d = (KorrekturVerf == 'A'),
 * depth = tool row +4 (D), ratio from eg_sharp_spiegel_params.  The input array is not modified
 * (the original rewrites z in its read buffer; this port works on a copy of each record). */
int eg_sharp_spiegel(const EgRecs *in, EgRecs *out, int only3d, double depth, double ratio);

/* FUN_00518bd4: in = VekAR8.TMP from the plain 2D offset FUN_0051001c; appends to out.
 * side_flag = byte the caller passes as 3rd arg (frame-0x58 of FUN_00528dac, same flag given to the
 * offset engine); depth = row+4 (D); half_angle_deg = row+0x10; r18 = row+0x18 (only checked). */
int eg_sharp_grund(const EgRecs *in, EgRecs *out, int side_flag, double depth,
                   double half_angle_deg, double r18);

/* helper */
int eg_recs_push(EgRecs *v, int tag, double x, double y, double z);


/* eg_sharp.c - port of the EStamp7.exe "Ausspitzen" (3D corner sharpening) pass.
 * Dump lines refer to decomp/engrave/est/p_4b0000.c (p_460000.c where noted); every float formula
 * was read from the disassembly (dis.py), not from the Ghidra C.  See NOTES.md.
 *
 * F32(): the original keeps records and most temporaries as 4-byte floats (fstp DWORD); x87
 * intermediate math is extended precision.  F32 rounds exactly where the original stores a float.
 */

int eg_sharp_f32 = 1;
static double F32(double v) { return eg_sharp_f32 ? (double)(float)v : v; }

int eg_recs_push(EgRecs *v, int tag, double x, double y, double z)
{
    if (v->n >= v->cap) {
        int nc = v->cap ? v->cap * 2 : 256;
        EgRec *nr = (EgRec *)realloc(v->r, (size_t)nc * sizeof(EgRec));
        if (!nr) return EG_SHARP_ERR_NOMEM;
        v->r = nr; v->cap = nc;
    }
    v->r[v->n].tag = tag; v->r[v->n].x = x; v->r[v->n].y = y; v->r[v->n].z = z;
    v->n++;
    return EG_SHARP_OK;
}

/* ---------------------------------------------------------------------------------------------
 * FUN_00517ac8 @00517ac8 (dump 74632-74668; disasm 517af4-517b70)
 *   local_8 = row+0x18 + offset                       (517af4: fld [row+0x18]; fadd [ebp+8]; fstp)
 *   FUN_005041dc("", "VekAR2.TMP", flag, 1, -1, 0, 0.0, local_8, W)   (517b13-517b2d)
 *   ratio = local_8 / (local_8 - (row+0xc + offset))  (517b32-517b49, stored as f32 argument)
 *   FUN_00517594("VekAR2.TMP", out, row+0x1c=='A', ratio, row+4, row+2)
 * ------------------------------------------------------------------------------------------- */
void eg_sharp_spiegel_params(double r18, double rc, double offset, double *L, double *ratio)
{
    double l = F32(r18 + offset);
    *L = l;
    *ratio = F32(l / (l - (rc + offset)));
}

/* ---------------------------------------------------------------------------------------------
 * Spiegel core state = the frame of FUN_00517594 (offsets are [ebp-x] of 517594).
 * ------------------------------------------------------------------------------------------- */
typedef struct {
    EgRecs *out;
    int only3d;          /* [ebp-0x3425] */
    double depth;        /* [ebp+0xc]  (row+4, D) */
    /* FUN_00517204 "last input" record [ebp-0x3434..] and pending-moveto flag [ebp-0x3435] */
    int ltag; double lx, ly, lz; int pending;
    /* FUN_0051710c "last written" copy [ebp-0x3424..] */
    int wtag; double wx, wy;
    int err;
} SpState;

/* FUN_0051710c @0051710c (dump 74257-74301; disasm 51710c-5171f3): buffered write of one record.
 * A move-to (0x0b) is dropped when the last written record (any tag != 0) is within 0.009 in x AND
 * y (517114-51715b, constant TBYTE@5171f8 = 0.009).  1000-record buffer + FUN_005170b8 flush
 * (only I/O; error 0x32 on short write) and FUN_0046e1d4/0046e05c (screen drawing) not ported. */
static void sp_write(SpState *s, int tag, double x, double y, double z)
{
    if (tag == EG_TAG_MOVE && s->wtag != 0 &&
        fabs(s->wx - x) < 0.009 && fabs(s->wy - y) < 0.009)
        return;
    if (eg_recs_push(s->out, tag, x, y, z)) s->err = EG_SHARP_ERR_NOMEM;
    s->wtag = tag; s->wx = x; s->wy = y;
}

/* FUN_00517204 @00517204 (dump 74304-74399; disasm 517204-517577): the 'A' (3D only) filter.
 * Dm = D - 0.02 (TBYTE@51757c).  Without only3d every record goes straight to FUN_0051710c.
 * With only3d only the parts of the path shallower than Dm are kept; crossings of z = Dm are
 * interpolated; a run that resumes starts with a move-to at z = 0 above the resume point. */
static void sp_put(SpState *s, int tag, double x, double y, double z)
{
    const double Dm = s->depth - 0.02;
    if (!s->only3d) {                                         /* 517210 -> 517531 */
        sp_write(s, tag, x, y, z);
    } else if (tag == EG_TAG_MOVE) {                          /* 51721d -> 517525 */
        s->pending = 1;
    } else if (s->lz >= z) {                                  /* 517229-517236 -> 5173d1 */
        if (!(Dm > z)) {                                      /* 5173d1-5173e4 */
            s->pending = 1;
        } else if (Dm >= s->lz) {                             /* 5173ea-517403 -> 5174cc */
            if (s->pending) { s->pending = 0; sp_write(s, EG_TAG_MOVE, s->lx, s->ly, 0.0); }
            sp_write(s, EG_TAG_POINT, x, y, z);
        } else {                                              /* 517409-5174ca: lz > Dm > z */
            double t = (Dm - s->lz) / (z - s->lz);
            double ix = F32((x - s->lx) * t + s->lx);
            double iy = F32((y - s->ly) * t + s->ly);
            sp_write(s, EG_TAG_MOVE, ix, iy, 0.0);
            s->pending = 0;
            sp_write(s, EG_TAG_POINT, ix, iy, F32(Dm));
            sp_write(s, EG_TAG_POINT, x, y, z);
        }
    } else {                                                  /* lz < z */
        if (!(Dm > s->lz)) {                                  /* 51723c-517255 -> 5173c2 */
            s->pending = 1;
        } else if (Dm >= z) {                                 /* 51725b-51726e -> 517372 */
            if (s->pending) { s->pending = 0; sp_write(s, EG_TAG_MOVE, s->lx, s->ly, 0.0); }
            sp_write(s, EG_TAG_POINT, x, y, z);
        } else if (fabs(s->lx - x) > 0.002 || fabs(s->ly - y) > 0.002) {   /* 517274-5172aa, TBYTE@517588 */
            double t;
            if (s->pending) { s->pending = 0; sp_write(s, EG_TAG_MOVE, s->lx, s->ly, 0.0); }
            t = (Dm - s->lz) / (z - s->lz);                  /* 5172e6-51730c */
            sp_write(s, EG_TAG_POINT, F32((x - s->lx) * t + s->lx), F32((y - s->ly) * t + s->ly),
                     F32(Dm));
        }
    }
    s->ltag = tag; s->lx = x; s->ly = y; s->lz = z;           /* 517546-51756d */
}

/* FUN_00517594 @00517594 (dump 74402-74629; disasm 517594-517aa4). */
int eg_sharp_spiegel(const EgRecs *in, EgRecs *out, int only3d, double depth, double ratio)
{
    SpState s;
    int ptag = EG_TAG_MOVE, nm = 1;                 /* [ebp-0x3458]=0x0b, [ebp-0x3457]=1 (517640) */
    double px = 0, py = 0, pz = 0, wx = 0, wy = 0;  /* [ebp-0x3444/-0x3448/-0x344c/-0x3450/-0x3454] */
    int i;

    s.out = out; s.only3d = only3d ? 1 : 0; s.depth = depth;
    s.ltag = 0; s.lx = s.ly = s.lz = 0; s.pending = 0;     /* 51767f-5176a5 */
    s.wtag = 0; s.wx = s.wy = 0;                           /* 5176a5-5176b4 copy of zeroed record */
    s.err = 0;

    for (i = 0; i < in->n; i++) {           /* 5000-record blocks (FUN_0046a680) - transparent */
        int tag = in->r[i].tag;
        double x = in->r[i].x, y = in->r[i].y, z;
        /* 517726-517761: skip a record equal (0.002, TBYTE@517aa8) to the previous one with same tag */
        if (!(fabs(px - x) > 0.002) && !(fabs(py - y) > 0.002) && tag == ptag)
            continue;
        z = F32((in->r[i].z - 1.0) * ratio + 1.0);          /* 517767-517779 */
        if (!(z > 0.0) && tag == EG_TAG_LINE) {             /* 51777c-517793 */
            if (pz > 0.0) {                                 /* 517799-5177a9 */
                double t;
                if (nm) {                                   /* 5177af-517800 */
                    sp_put(&s, EG_TAG_MOVE, px, py, 0.0);
                    nm = 0;
                    sp_put(&s, EG_TAG_POINT, px, py, F32(pz * depth + 0.0));
                }
                t = pz / (pz - z);                          /* 517801-517815 */
                wx = F32((x - px) * t + px);                /* 51781b-517831 */
                wy = F32((y - py) * t + py);                /* 517837-51784d */
                sp_put(&s, EG_TAG_POINT, wx, wy, 0.0);      /* 517854-517865 */
            }
        } else if (tag == EG_TAG_LINE) {                    /* 517870 */
            int ntag;
            if (nm && pz > 0.0) {                           /* 517879-5178ef */
                sp_put(&s, EG_TAG_MOVE, px, py, 0.0);
                nm = 0;
                sp_put(&s, EG_TAG_POINT, px, py, F32(pz * depth + 0.0));
                wx = px; wy = py;
            }
            if (pz < 0.0) {                                 /* 5178f5-517905 */
                double t = pz / (pz - z);                   /* 51790b-51791f */
                px = F32((x - px) * t + px);                /* 517925-51793b (overwrites prev) */
                py = F32((y - py) * t + py);
                if (fabs(px - wx) > 0.002 || fabs(py - wy) > 0.002) {   /* 51795d-517994 */
                    sp_put(&s, EG_TAG_MOVE, px, py, 0.0);
                    nm = 0;
                    sp_put(&s, EG_TAG_POINT, px, py, 0.0);
                }
                ntag = EG_TAG_POINT;                        /* 5179cb */
            } else {                                        /* 5179cf-5179f4, TBYTE@517abc = 0.99 */
                ntag = (z > 0.99 && pz > 0.99) ? EG_TAG_LINE : EG_TAG_POINT;
            }
            sp_put(&s, ntag, x, y, F32(z * depth + 0.0));   /* 5179f6-517a15 */
            wx = x; wy = y;                                 /* 517a16-517a22 */
        } else {
            nm = 1;                                         /* 517a2a */
        }
        ptag = tag; px = x; py = y; pz = z;                 /* 517a31-517a4f */
        if (s.err) return s.err;
    }
    return s.err;
}

/* =============================================================================================
 * Grund: FUN_00518bd4 and its nested helpers.  Slot = 0x1f-byte struct in the frame of 518bd4:
 *   +1 steep flag, +2 tag, +3 x, +7 y, +0xb z, +0xf dx, +0x13 dy, +0x17 slope, +0x1b direction angle
 *   S[0] @[F-0x68], S[1] @[F-0x49], S[2] @[F-0x2a], P (2nd point of current loop) @[F-0x90].
 * ============================================================================================= */
typedef struct { int steep; int tag; double x, y, z; double dx, dy, slope, ang; } Slot;

static const Slot SLOT0 = {0, 0, 0, 0, 0, 0, 0, 0, 0};

/* FUN_00518738 @00518738 + FUN_005186b8 @005186b8 (dump 75314-75323, 75287-75311;
 * disasm 518738-518751, 5186b8-518732) */
static void gr_seg(Slot *p, const Slot *q)
{
    p->dx = F32(q->x - p->x);
    p->dy = F32(q->y - p->y);
    if (fabs(p->dx) > fabs(p->dy)) {                  /* 5186c1-5186d8 */
        p->steep = 0;
        p->slope = (p->dx == 0.0) ? 0.0 : F32(p->dy / p->dx);
    } else {
        p->steep = 1;
        p->slope = (p->dy == 0.0) ? 0.0 : F32(p->dx / p->dy);
    }
}

/* FUN_00518754 @00518754 (dump 75326-75367; disasm 518754-518815): direction angle in [0,2pi)
 * from the slope via ArcTan (FUN_00403370); constants TBYTE@51881c/518828/518834/518840. */
static void gr_ang(Slot *p)
{
    double a = atan(p->slope);
    if (p->steep) {
        if (p->dy >= 0.0) p->ang = F32(1.5707963267948966 - a);
        else              p->ang = F32(4.71238898038469 - a);
    } else if (p->dx < 0.0) {
        p->ang = F32(3.141592653589793 + a);
    } else if (p->dy < 0.0) {
        p->ang = F32(6.283185307179586 + a);
    } else {
        p->ang = F32(a);
    }
}

/* FUN_0051884c @0051884c (disasm 51884c-5188a3; Ghidra shows an empty body because the result is
 * returned in ST0): d = a1 - a0 normalised into [-pi, pi] (TBYTE@5188a8 = pi, @5188b8 = 2pi). */
static double gr_turn(double a0, double a1)
{
    double d = F32(a1 - a0);
    if (!(3.141592653589793 >= fabs(d))) {
        if (d >= 0.0) d = F32(d - 6.283185307179586);
        else          d = F32(6.283185307179586 + d);
    }
    return d;
}

/* FUN_004a1b40 @004a1b40 (p_460000.c dump 51075-51100; disasm 4a1b40-4a1bc5):
 * (ox,oy) = dist * (-(Q.y-P.y), Q.x-P.x) / |Q-P|  (left normal), 0 for a zero-length segment. */
static void gr_perp(double dist, double Px, double Py, double Qx, double Qy, double *ox, double *oy)
{
    double a = F32(Qx - Px), b = F32(Qy - Py);
    double len = F32(sqrt(a * a + b * b));
    if (len == 0.0) { *ox = 0.0; *oy = 0.0; return; }
    *ox = F32(-dist * b / len);
    *oy = F32(dist * a / len);
}

/* FUN_004a1f04 @004a1f04 (p_460000.c dump 51307-51323; disasm 4a1f04-4a1f54): tan, 0 for 0. */
static double gr_tan(double a)
{
    return (a == 0.0) ? 0.0 : F32(sin(a) / cos(a));
}

/* FUN_005188c4 @005188c4 (dump 75381-75406; disasm 5188c4-518956): intersection of line
 * A1->B1 with the line through B2 parallel to C2->B2.  Returns 0 if parallel (den == 0). */
static int gr_isect(double A1x, double A1y, double B1x, double B1y, double B2x, double B2y,
                    double C2x, double C2y, double *ix, double *iy)
{
    double ux = F32(B1x - A1x), uy = F32(B1y - A1y);
    double vx = F32(B2x - C2x), vy = F32(B2y - C2y);
    double den = F32(uy * vx - ux * vy), t;
    if (den == 0.0) return 0;
    t = F32(((B2y - A1y) * vx - (B2x - A1x) * vy) / den);
    *ix = F32(t * ux + A1x);
    *iy = F32(t * uy + A1y);
    return 1;
}

typedef struct {
    EgRecs *out;
    int side_flag;      /* [F-0x69] */
    double ang;         /* [F-0x70] = half angle in rad, f32 */
    Slot S[3], P;
    int count;          /* [F-0x71] (byte) */
    int err;
} GrState;

/* FUN_00518650 @00518650 (dump 75261-75284): append one record (5000-record buffer, flushed by
 * FUN_00518610 into the per-tool file; FUN_0046e3e4 = screen drawing, not ported). */
static void gr_write(GrState *g, int tag, double x, double y, double z)
{
    if (eg_recs_push(g->out, tag, x, y, z)) g->err = EG_SHARP_ERR_NOMEM;
}

/* FUN_00518960 @00518960 (dump 75409-75491; disasm 518960-518b2c): emit the middle point S[1];
 * if the path turns by more than 0.6283185 rad (36 deg, TBYTE@518b30) towards the side selected by
 * side_flag and S[1].z >= 0.01 (TBYTE@518b3c), add the move S[1] -> apex (z = 0) -> S[1]. */
static void gr_corner(GrState *g)
{
    const Slot *A = &g->S[0], *B = &g->S[1], *C = &g->S[2];
    double d, s, tn, d1, d2, d3, o1x, o1y, o2x, o2y, o3x, o3y, o4x, o4y, ix, iy;

    gr_write(g, B->tag, B->x, B->y, B->z);                     /* 51896d-51897e */
    d = gr_turn(A->ang, B->ang);                               /* 518984-51898f */
    if (g->side_flag) { d = -d; s = 1.0; } else s = -1.0;      /* 518993-5189ae */
    if (!(d > 0.6283185307179586)) return;                     /* 5189b5-5189c2 */
    if (0.01 > B->z) return;                                   /* 5189c8-5189d5 */
    tn = gr_tan(g->ang);                                       /* 5189db-5189e1 */
    d1 = F32(s * A->z * tn);                                   /* 5189e6-5189ee */
    gr_perp(d1, A->x, A->y, B->x, B->y, &o1x, &o1y);           /* 5189f2-518a07 */
    d2 = F32(s * B->z * tn);                                   /* 518a0c-518a1f */
    gr_perp(d2, A->x, A->y, B->x, B->y, &o2x, &o2y);           /* 518a23-518a38 */
    gr_perp(d2, B->x, B->y, C->x, C->y, &o3x, &o3y);           /* 518a3d-518a52 */
    d3 = F32(s * C->z * tn);                                   /* 518a57-518a6a */
    gr_perp(d3, B->x, B->y, C->x, C->y, &o4x, &o4y);           /* 518a6e-518a83 */
    if (!gr_isect(F32(A->x + o1x), F32(A->y + o1y), F32(B->x + o2x), F32(B->y + o2y),
                  F32(B->x + o3x), F32(B->y + o3y), F32(C->x + o4x), F32(C->y + o4y),
                  &ix, &iy))                                   /* 518a88-518afd */
        return;
    gr_write(g, EG_TAG_POINT, ix, iy, 0.0);                    /* 518aff-518b0d */
    gr_write(g, EG_TAG_POINT, B->x, B->y, B->z);               /* 518b13-518b22 */
}

/* FUN_00518b48 @00518b48 (dump 75494-75540; disasm 518b48-518bd0): close the loop - shift the
 * window and use the saved 2nd point P as the successor of the last point. */
static void gr_close(GrState *g)
{
    if (g->count > 2 && g->P.tag != 0) {
        g->S[0] = g->S[1];
        g->S[1] = g->S[2];
        g->S[2] = g->P;
        gr_seg(&g->S[1], &g->S[2]);
        gr_ang(&g->S[1]);
        gr_corner(g);
    }
}

/* FUN_00518bd4 @00518bd4 (dump 75543-75764; disasm 518bd4-518f5b). */
int eg_sharp_grund(const EgRecs *in, EgRecs *out, int side_flag, double depth,
                   double half_angle_deg, double r18)
{
    GrState g;
    double D = F32(depth);
    int i;

    /* 518c56-518cac: error 0x36 if D < 0.01, angle < 1 deg or row+0x18 < 0.01 (TBYTE@518f60) */
    if (0.01 > D || half_angle_deg < 1.0 || 0.01 > r18) return EG_SHARP_ERR_54;

    g.out = out; g.side_flag = side_flag ? 1 : 0; g.err = 0;
    g.ang = F32(0.017453292519943295 * F32(half_angle_deg));    /* 518cb1-518cbd, TBYTE@518f70 */
    g.count = 0;                                                /* 518cdd */
    g.S[0] = g.S[1] = g.S[2] = SLOT0;                           /* 518ce1: 0x5d bytes zeroed */
    g.P = SLOT0;                                                /* 518cf0: 0x1f bytes zeroed */

    for (i = 0; i < in->n; i++) {
        Slot r = SLOT0;
        r.tag = in->r[i].tag; r.x = in->r[i].x; r.y = in->r[i].y; r.z = in->r[i].z;
        if (r.tag == EG_TAG_MOVE) {                             /* 518d72-518dc4 */
            gr_close(&g);
            g.S[0] = SLOT0;
            gr_write(&g, r.tag, r.x, r.y, r.z);                 /* emitted with its own z */
            r.z = D;                                            /* 518d9b-518da1 */
            g.S[0] = r;
            g.count = 1;
        } else if (r.tag == EG_TAG_LINE || r.tag == EG_TAG_POINT) {   /* 518dc9 */
            if (r.tag == EG_TAG_LINE) r.z = D;                  /* 518dc9-518dd3 (0x0c keeps z) */
            if (g.count == 1) g.P = r;                          /* 518dd6-518df8 */
            if (g.count <= 2) {                                 /* 518dfb-518e6b */
                if (g.count == 0) return EG_SHARP_ERR_NOT_DECODED; /* original writes slot[-1] into
                                                               its own locals (count, angle) */
                g.S[g.count] = r;
                gr_seg(&g.S[g.count - 1], &g.S[g.count]);
                gr_ang(&g.S[g.count - 1]);
                g.count++;
            } else {                                            /* 518e70-518ed1 */
                g.S[0] = g.S[1];
                g.S[1] = g.S[2];
                g.S[2] = r;
                gr_seg(&g.S[1], &g.S[2]);
                gr_ang(&g.S[1]);
            }
            if (g.count > 2) gr_corner(&g);                     /* 518ed6-518ee2 */
        } else {                                                /* 518ee5-518f02 */
            gr_close(&g);
            g.count = 0;
            gr_write(&g, r.tag, r.x, r.y, r.z);
        }
        if (g.err) return g.err;
    }
    gr_close(&g);                                               /* 518f1c */
    return g.err;
}

#undef F32
#undef GrState
#undef SLOT0
#undef Slot
#undef SpState
#undef eg_recs_free
#undef eg_recs_push
#undef eg_recs_push_rec
#undef gr_ang
#undef gr_close
#undef gr_corner
#undef gr_isect
#undef gr_perp
#undef gr_seg
#undef gr_tan
#undef gr_turn
#undef gr_write
#undef sp_put
#undef sp_write

/* ================================================================================================================== */
/* the roughing "Abraeumen" zigzag / one way FUN_00528104 (formats/engrave_engine.md 6)
   (port of eg_rough.c, decoded from EStamp7.exe; renamed file-scope names: rgh_*) */
#define EgBox rgh_EgBox
#define HB rgh_HB
#define HCore rgh_HCore
#define HLink rgh_HLink
#define HRow rgh_HRow
#define R13 rgh_R13
#define R9 rgh_R9
#define R9s rgh_R9s
#define Rnd rgh_Rnd
#define SubP rgh_SubP
#define XI rgh_XI
#define box_4f378c rgh_box_4f378c
#define box_510438 rgh_box_510438
#define box_setup rgh_box_setup
#define conv_13_to_9 rgh_conv_13_to_9
#define conv_9_to_13 rgh_conv_9_to_13
#define cross_4f2e08 rgh_cross_4f2e08
#define cross_fin_511194 rgh_cross_fin_511194
#define eg_recs_free rgh_eg_recs_free
#define eg_recs_push rgh_eg_recs_push
#define eg_recs_push_rec rgh_eg_recs_push_rec
#define emit_51380c rgh_emit_51380c
#define flush_4f3094 rgh_flush_4f3094
#define flush_513944 rgh_flush_513944
#define gate_4f2a44 rgh_gate_4f2a44
#define hatch_4f39e0 rgh_hatch_4f39e0
#define index_510c6c rgh_index_510c6c
#define joint_4f2f20 rgh_joint_4f2f20
#define link_4f3334 rgh_link_4f3334
#define mv_4f2644 rgh_mv_4f2644
#define obuf_add rgh_obuf_add
#define r9_push rgh_r9_push
#define recs_copy rgh_recs_copy
#define rot_4f24ec rgh_rot_4f24ec
#define round_513b7c rgh_round_513b7c
#define rowend_4f29e8 rgh_rowend_4f29e8
#define rows_4f2a80 rgh_rows_4f2a80
#define write_510f84 rgh_write_510f84
#define x_4f27b0 rgh_x_4f27b0
/* eg_rough.h - port of the EStamp7.exe (HCAM engrave engine) roughing ("Abraeumen") for
 * AbraeumVerf 'Z' (zeilebi, zigzag) and 'Y' (zeileuni, one way) plus the merge of the roughing
 * into the tool output files.  Every rule is cited in eg_rough.c / NOTES.md
 * (function address + dump line of decomp/engrave/est/p_4b0000.c).
 *
 * Units: whatever the engine gets (mm).  Records mirror the 13-byte LG1 records
 * (tag u8 + x, y, z f32); internally coordinates are rounded to float where the original
 * stores a DWORD, so output can be compared point by point.
 */


/* tags (brief: LG1 layout) */
#define EG_TAG_LINE  0x0a
#define EG_TAG_MOVE  0x0b
#define EG_TAG_POINT 0x0c

/* return codes of the port (not engine codes) */
#define EG_ROUGH_OK            0
#define EG_ROUGH_ENOMEM       -1
#define EG_ROUGH_EARG         -2
#define EG_ROUGH_NOT_DECODED  -3   /* a path that needs undecoded engine code */

/* engine error codes written by the hatch core FUN_004f39e0 into its error byte; FUN_0051064c /
 * FUN_005108d4 store (code + 0x14) into the global error *(int*)PTR_DAT_0052f230. */
#define EG_HATCH_ERR_EMPTY     0x15 /* boundary file has no records          (4f39e0 l.45777) */
#define EG_HATCH_ERR_BOX       0x14 /* boundary outside the given box        (4f39e0 l.45796) */
#define EG_HATCH_ERR_ANGLE     0x19 /* |angle| >= 2 pi                       (4f39e0 l.45781) */
#define EG_HATCH_ERR_BAND      0x17 /* band height halved below 750 records  (4f39e0 l.45983) */
#define EG_HATCH_ERR_SEGS      0x18 /* more than 45000 hatch segments        (4f3334 l.45405) */

void eg_recs_free(EgRecs *a);
int  eg_recs_push(EgRecs *a, int tag, double x, double y, double z);

/* Global box PTR_DAT_0052ed80 (18 bytes: u8, f32 xmin @+1, f32 ymin @+5, u8, f32 xmax @+0xa,
 * f32 ymax @+0xe), the drawing box (FUN_00529514 l.87489-87508). */
typedef struct { float xmin, ymin, xmax, ymax; } EgBox;

/* FUN_0051064c: zigzag hatch of the roughing boundary (VekAR10.TMP = 2D offset of the prepared
 * contours by maxR, from FUN_0051001c).
 *   boundary   closed loops (0x0b start, 0x0a/0x0c points), the VekAR10 content
 *   frame      RAHMEN.TMP content (FUN_004de528) or NULL/empty
 *   gravur_v   GravurTyp == 'V'
 *   gbox       the drawing box PTR_DAT_0052ed80
 *   angle      hatch angle as the engine receives it (radians; |angle| < 2 pi, see NOTES)
 *   spacing    (2 - overlap/50) * R[slot].rt  (FUN_00528104 l.86835-86836)
 *   e_stack    NOT DECODED: the hatch struct field +0x14 is an uninitialised stack float in
 *              FUN_0051064c; the code makes it max(e_stack, spacing/4).  Pass 0 (measured:
 *              op4 row ends sit ~spacing/4 inside the boundary).
 *   out        receives the new VekAR10 content (hatch records, z = 0).  When the engine
 *              leaves VekAR10 unchanged (boundary <= 3 records, or an error) out = boundary.
 *   eng_err    engine global error (0 or code + 0x14)
 */
int eg_rough_hatch_zigzag_51064c(const EgRecs *boundary, const EgRecs *frame, int gravur_v,
                                 const EgBox *gbox, float angle, float spacing, float e_stack,
                                 EgRecs *out, int *eng_err);

/* FUN_005108d4: one-way ('Y') hatch, appended to the roughing tool file (file(10)). */
int eg_rough_hatch_oneway_5108d4(const EgRecs *boundary, const EgRecs *frame, int gravur_v,
                                 const EgBox *gbox, float angle, float spacing,
                                 EgRecs *tool10_append, int *eng_err);

/* FUN_0051170c -> FUN_00511310: order the subpaths of the hatch file (nearest neighbour) and
 * append them to the roughing tool file, joining consecutive subpaths (0x0b -> 0x0a) when
 *   (distance < 0.7 * spacing or kzv) and the joint does not cross the finishing tool path.
 *   connect  1 for 'Z' (start with the first subpath, scan ascending), 0 for 'C'/'D'
 *   kzv      KZeilenVerbinden (block 7 +0x1a)
 *   finish   content of the finishing tool file of slot R[slot] (FUN_004c3ae0)
 *   short_err (out) engine short error *(short*)PTR_DAT_0052f2e4 (0x44f no data, 0x44d full) */
int eg_rough_merge_51170c(const EgRecs *hatch, const EgRecs *finish, int connect, int kzv,
                          float spacing, EgRecs *tool10_append, int *short_err);

/* FUN_00527dfc: append the 0x0a..0x0c records of src to dst. */
int eg_rough_append_527dfc(const EgRecs *src, EgRecs *dst);

/* FUN_00513e38: corner rounding of a path file (KAbrZeilenRund tail of FUN_0052919c,
 * nseg = 5, d = spacing / 2.5).  out is the new file content. */
int eg_rough_round_513e38(const EgRecs *in, int nseg, float d, EgRecs *out);

/* FUN_00528104 'Z' / 'Y' after the boundary offset (FUN_0051001c, other module), incl. the
 * KAbrWegZusammen merge and the FUN_0052919c KAbrZeilenRund tail.
 *   finish   in/out: finishing tool file of slot param_13
 *   tool10   in/out: roughing tool file (slot 10)  */
typedef struct {
    char  verf;            /* 'Z' or 'Y' */
    int   gravur_v;        /* GravurTyp == 'V' */
    EgBox gbox;
    float angle;           /* radians as received */
    float overlap;         /* block 7 +8 KAbraeumueberlapp (%) */
    float rt;              /* R[slot] +0xc, tip radius of the roughing slot */
    int   kzv;             /* block 7 +0x1a KZeilenVerbinden */
    int   abr_zuerst;      /* block 7 +0x19 KAbraeumZuerst */
    int   abr_weg_zus;     /* block 7 +0x1c KAbrWegZusammen */
    int   abr_zeilen_rund; /* block 7 +0x1d KAbrZeilenRund */
    float e_stack;         /* see eg_rough_hatch_zigzag_51064c */
} EgRoughParams;

int eg_rough_528104(const EgRoughParams *p, const EgRecs *boundary, const EgRecs *frame,
                    EgRecs *finish, EgRecs *tool10, int *eng_err);


/* eg_rough.c - port of EStamp7.exe roughing 'Z' / 'Y' (see eg_rough.h, NOTES.md).
 * Citations "l.N" are line numbers of decomp/engrave/est/p_4b0000.c; "@addr" are code
 * addresses (dis.py) where the float math / argument order was read from the disassembly.
 * Float handling: values the original stores as DWORD are rounded to float here; x87
 * intermediates are computed in double (the original keeps 80-bit), Y of the hatch rows is a
 * double as in the original (QWORD [ebp-0x2498]).
 */

#define F(v) ((float)(v))

void eg_recs_free(EgRecs *a) { free(a->r); a->r = NULL; a->n = a->cap = 0; }

int eg_recs_push(EgRecs *a, int tag, double x, double y, double z)
{
    if (a->n == a->cap) {
        int nc = a->cap ? a->cap * 2 : 256;
        EgRec *p = (EgRec *)realloc(a->r, (size_t)nc * sizeof(EgRec));
        if (!p) return EG_ROUGH_ENOMEM;
        a->r = p; a->cap = nc;
    }
    a->r[a->n].tag = tag; a->r[a->n].x = x; a->r[a->n].y = y; a->r[a->n].z = z;
    a->n++;
    return EG_ROUGH_OK;
}

static int recs_copy(const EgRecs *s, EgRecs *d)
{
    d->n = 0;
    for (int i = 0; s && i < s->n; i++)
        if (eg_recs_push(d, s->r[i].tag, s->r[i].x, s->r[i].y, s->r[i].z)) return EG_ROUGH_ENOMEM;
    return EG_ROUGH_OK;
}

/* ---- 9-byte records (tag + x, y f32) used by the hatch core ("VekH2R*.H2R", HELP.TMP) ---- */
typedef struct { unsigned char tag; float x, y; } R9;
typedef struct { R9 *r; int n, cap; } R9s;

static int r9_push(R9s *a, unsigned char tag, float x, float y)
{
    if (a->n == a->cap) {
        int nc = a->cap ? a->cap * 2 : 256;
        R9 *p = (R9 *)realloc(a->r, (size_t)nc * sizeof(R9));
        if (!p) return EG_ROUGH_ENOMEM;
        a->r = p; a->cap = nc;
    }
    a->r[a->n].tag = tag; a->r[a->n].x = x; a->r[a->n].y = y; a->n++;
    return EG_ROUGH_OK;
}

/* FUN_004f54fc (l.46788): 13-byte -> 9-byte records (drops z), append when param_3. */
static int conv_13_to_9(const EgRecs *s, R9s *d)
{
    for (int i = 0; s && i < s->n; i++)
        if (r9_push(d, (unsigned char)s->r[i].tag, F(s->r[i].x), F(s->r[i].y))) return EG_ROUGH_ENOMEM;
    return EG_ROUGH_OK;
}

/* FUN_004f5348 (l.46703): 9-byte -> 13-byte records, z := 0 (l.46770); rewrite or append. */
static int conv_9_to_13(const R9s *s, EgRecs *d)
{
    for (int i = 0; i < s->n; i++)
        if (eg_recs_push(d, s->r[i].tag, s->r[i].x, s->r[i].y, 0.0)) return EG_ROUGH_ENOMEM;
    return EG_ROUGH_OK;
}

/* FUN_004f24ec (l.44625-44664, consts @4f2581 pi): rotate (x, y) about the origin by a.
 * Polar form: r = sqrt(x^2+y^2), phi = atan(y/x) (+pi if x < 0, +-pi/2 if x == 0),
 * phi + a is stored as float (@4f2594), then x = cos * r, y = sin * r. */
static void rot_4f24ec(float *x, float *y, float a)
{
    if (a == 0.0f) return;
    float r = F(sqrt((double)*x * *x + (double)*y * *y));
    float phi;
    if (*x == 0.0f) phi = (*y <= 0.0f) ? -1.5707964f : 1.5707964f;
    else {
        phi = F(atan((double)*y / (double)*x));
        if (*x < 0.0f) phi = F(3.141592653589793 + phi);
    }
    float pa = F(phi + a);
    *x = F(cos((double)pa) * r);
    *y = F(sin((double)pa) * r);
}

/* ======================= hatch core FUN_004f39e0 and nested procedures ======================= */

/* param block built by FUN_0051064c / FUN_005108d4 (51064c @51076a-5107dc):
 * +0 xmin, +4 xmax, +8 ymax, +0xc ymin, +0x10 spacing s, +0x14 end inset e, +0x18 angle,
 * +0x1c flag (cut the intervals between crossings 1-2, 3-4, ...; else the complement),
 * +0x1d zigzag linking via HELP.TMP, +0x1f one-way rows. */
typedef struct {
    float xmin, xmax, ymax, ymin, s, e, angle;
    unsigned char flag, link, oneway;
} HB;

typedef struct { unsigned char valid; float x; } XI;   /* 5-byte crossing list (P-0x249c) */

typedef struct {
    HB *b;
    unsigned char *err;
    /* strip buffer P-0x248c / count P-0x247c */
    R9 *buf; int cnt;
    /* crossings */
    XI *xs; int nx;
    double Y;            /* P-0x2498 */
    float s20;           /* P-0x24a0 = s/20       (@4f3db0) */
    float stripLow;      /* P-0x24a4 */
    float far2;          /* P-0x24a8 = (36 s)^2   (@4f3dc8) */
    float k19;           /* P-0x24ac = 1.9 s      (@4f3dd6) */
} HCore;

/* FUN_004f27b0 (l.44762-44833, @4f27b0): crossings of row Y with the strip records.
 * Returns tries (F-0x24); *dir and *first_shift are 4f2a80 locals F-0x25 / F-0x26. */
static int x_4f27b0(HCore *h, unsigned char *dir, unsigned char *first_shift)
{
    int tries = 0;
    h->nx = 0;
    if (h->cnt > 1) {
        int retry;
        do {
            float px = h->buf[0].x, py = h->buf[0].y;
            int hit = ((double)py == h->Y);            /* @4f2806 */
            int i = 1;
            if (!hit) {
                for (;;) {
                    const R9 *rc = &h->buf[i];
                    if ((double)rc->y == h->Y) { hit = 1; break; }        /* @4f282e */
                    if (rc->tag == 0x0a || rc->tag == 0x0c) {             /* @4f284a, 0x0b skipped */
                        double d = (double)rc->y - (double)py;
                        if (d != 0.0) {
                            double t = (h->Y - (double)py) / d;
                            if (t > 0.0 && t < 1.0) {                     /* @4f289a / @4f28a9 */
                                h->xs[h->nx].x = F(((double)rc->x - (double)px) * t + (double)px);
                                h->xs[h->nx].valid = 1;
                                h->nx++;
                            }
                        }
                    }
                    px = rc->x; py = rc->y;
                    i++;
                    if (i >= h->cnt) break;
                }
            }
            retry = 0;
            if ((h->nx & 1) == 1 || hit) {                 /* @4f2900: odd count or vertex hit */
                tries++;
                if (tries > 10) *dir ^= 1;                  /* @4f2920 */
                h->nx = 0;
                retry = 1;
                if (*first_shift) h->Y -= (double)h->s20;  /* @4f2942 */
                else              h->Y += (double)h->s20;  /* @4f2969 */
            }
            if (!(retry && tries <= 10)) break;
        } while (1);
    }
    if (tries <= 10) *first_shift = 0;                     /* @4f29a8 */
    return tries;
}

typedef struct {
    HCore *h;
    R9s *out;
    R9 rec0, rec1;          /* F-0x14 / F-0xb */
    unsigned char pend;     /* F-0x15 */
    unsigned char dir;      /* F-0x25 */
} HRow;

/* FUN_004f2644 (l.44698-44759, @4f2644): kind 0 = remember a move-to (0x0b) at (x, Y);
 * kind != 0 = line-to (0x0a): written together with the pending move-to.  Without +0x1d the
 * records are rotated back by +angle here (@4f26b5, @4f2728, @4f278b). */
static int mv_4f2644(HRow *w, unsigned char kind, float x)
{
    HB *b = w->h->b;
    if (kind == 0) {
        w->rec0.tag = 0x0b; w->rec0.x = x; w->rec0.y = F(w->h->Y);
        if (!b->link) rot_4f24ec(&w->rec0.x, &w->rec0.y, b->angle);
        w->pend = 1;
        return 0;
    }
    w->rec1.tag = 0x0a; w->rec1.x = x; w->rec1.y = F(w->h->Y);
    if (!b->link) rot_4f24ec(&w->rec1.x, &w->rec1.y, b->angle);
    if (w->pend) {
        if (r9_push(w->out, w->rec0.tag, w->rec0.x, w->rec0.y)) return EG_ROUGH_ENOMEM;
        if (r9_push(w->out, w->rec1.tag, w->rec1.x, w->rec1.y)) return EG_ROUGH_ENOMEM;
        w->pend = 0;
    } else {
        if (r9_push(w->out, w->rec1.tag, w->rec1.x, w->rec1.y)) return EG_ROUGH_ENOMEM;
    }
    return 0;
}

/* FUN_004f2a44 (l.44864, @4f2a44): emit only when the new point lies beyond the previous one in
 * the sweep direction: ((pnew > pprev) == dir) -> 4f2644(flag ^ k, pnew). */
static int gate_4f2a44(HRow *w, unsigned char k, float pprev, float pnew)
{
    unsigned char gt = (pnew > pprev) ? 1 : 0;
    if ((gt ^ w->dir) == 0) return mv_4f2644(w, (unsigned char)(w->h->b->flag ^ k), pnew);
    return 0;
}

/* FUN_004f29e8 (l.44848): row end at the far box side, kind flag ^ 1. */
static int rowend_4f29e8(HRow *w)
{
    HB *b = w->h->b;
    return mv_4f2644(w, (unsigned char)(b->flag ^ 1), w->dir ? b->xmax : b->xmin);
}

/* FUN_004f2a80 (l.44878-45069, @4f2a80): scan rows from Y downwards in steps of s over the
 * current strip.  p2 = 0: alternate the direction per row (zigzag); p2 = 1: always +x. */
static int rows_4f2a80(HCore *h, R9s *out, unsigned char p2)
{
    HB *b = h->b;
    HRow w; memset(&w, 0, sizeof w);
    w.h = h; w.out = out;
    w.pend = 0;                 /* @4f2a8e */
    w.dir = p2;                 /* @4f2a96 */
    unsigned char a = 1, first = 1, first_shift = 1, once = 1;   /* F-0x39, F-0x3a, F-0x26, F-0x3b */
    float pA = 0.0f, pB = 0.0f; /* F-0x2c, F-0x30 */
    int rc;
    for (;;) {
        if (p2 == 0) w.dir ^= 1;                                   /* @4f2aa9 */
        int tries = x_4f27b0(h, &w.dir, &first_shift);
        if (tries <= 10) {                                         /* @4f2abf */
            if (first) {                                           /* @4f2ac9 */
                if ((rc = mv_4f2644(&w, 0, b->xmin))) return rc;
                pA = pB = b->xmin; first = 0;
            } else if (a) {                                        /* @4f2afc, FUN_004f29c0 l.44836 */
                float x = w.dir ? b->xmin : b->xmax;
                if ((rc = mv_4f2644(&w, 0, x))) return rc;
                pA = pB = x;
            }
            if (h->nx <= 0) {                                      /* @4f2b29 */
                if ((rc = rowend_4f29e8(&w))) return rc;
                a = 1;
            } else {
                int found;
                do {
                    float lim = w.dir ? b->xmax : b->xmin;         /* @4f2b33 */
                    found = h->nx;
                    for (int i = 0; i < h->nx; i++) {
                        if (!h->xs[i].valid) continue;
                        if (w.dir) { if (h->xs[i].x <= lim) { lim = h->xs[i].x; found = i; } }
                        else       { if (h->xs[i].x >= lim) { lim = h->xs[i].x; found = i; } }
                    }
                    if (found < h->nx) {                           /* @4f2ba3 */
                        h->xs[found].valid = 0;
                        if (lim > b->xmin && lim < b->xmax) {      /* @4f2bbc / @4f2bd1 */
                            if (a) {                               /* entering */
                                a = 0;
                                pB = (b->flag ^ w.dir) ? F(lim - b->e) : F(lim + b->e);
                                if ((rc = gate_4f2a44(&w, 1, pA, pB))) return rc;
                            } else {                               /* leaving */
                                a = 1;
                                pA = (b->flag ^ w.dir) ? F(lim + b->e) : F(lim - b->e);
                                if ((rc = gate_4f2a44(&w, 0, pB, pA))) return rc;
                            }
                        }
                    } else if (a) {
                        if ((rc = rowend_4f29e8(&w))) return rc;   /* @4f2c79 */
                    }
                } while (found < h->nx);
            }
        }
        /* next row (@4f2c9c) */
        h->Y -= (double)b->s;
        if (h->Y <= (double)h->stripLow &&
            (double)h->stripLow - h->Y < (double)b->s / 2.0)            /* @4f2cc4-4f2cfc */
            h->Y = (double)b->s / 8.0 + (double)h->stripLow;            /* @4f2cfe */
        float v = F((double)b->ymin - h->Y);                            /* @4f2d25 */
        if (once && b->link && v >= 0.0f && F(b->s * 0.75f) >= v) {      /* @4f2d3b-4f2d72 */
            h->Y = 0.01 + (double)b->ymin;                              /* @4f2d74 (TBYTE 0.01) */
            once = 0;
        }
        /* @4f2d91: user abort (FUN_004bc3c0) -> error 3; not ported */
        if (h->Y <= (double)h->stripLow || *h->err != 0) {              /* @4f2da9 */
            h->stripLow = F(h->Y);
            return 0;
        }
    }
}

/* FUN_004f2e08 (l.45072-45106, @4f2e08, consts -0.0001 / 0.9999 @4f2f08 / @4f2f14):
 * does the joint hi->lo cross the boundary segment p1->p2? */
static int cross_4f2e08(const R9 *hi, const R9 *lo, const R9 *p1, const R9 *p2, int *res)
{
    float d1x = F((double)lo->x - hi->x), d1y = F((double)lo->y - hi->y);
    float d2x = F((double)p1->x - p2->x), d2y = F((double)p1->y - p2->y);
    float den = F((double)d1y * d2x - (double)d1x * d2y);
    if (den == 0.0f) return 0;
    double t = (((double)p1->y - hi->y) * d2x - ((double)p1->x - hi->x) * d2y) / den;
    if (-0.0001 > t || 0.9999 < t) return 0;
    double u = (((double)p1->x - hi->x) * d1y - ((double)p1->y - hi->y) * d1x) / den;
    *res = (-0.0001 > u || 0.9999 < u) ? 0 : 1;
    return 1;
}

/* FUN_004f2f20 (l.45109-45194, @4f2f20): 1 when the straight joint a->b crosses any segment of
 * the strip records (consecutive records, tags not checked: also the jump between contours). */
static int joint_4f2f20(const HCore *h, const R9 *a, const R9 *b)
{
    if (h->cnt <= 1) return 0;
    R9 hi = *a, lo = *b;
    if (a->y < b->y) { hi = *b; lo = *a; }                 /* @4f2f53 */
    R9 prev = h->buf[0];
    int dl = 3;
    if (prev.y > hi.y) dl = 1; else if (prev.y < lo.y) dl = 2;
    for (int i = 1; i < h->cnt; i++) {
        const R9 *rc = &h->buf[i];
        int bl = 3;
        if (rc->y > hi.y) bl = 1; else if (rc->y < lo.y) bl = 2;
        if ((dl | bl) > 2) {                               /* @4f3029 */
            int res = 0;
            if (cross_4f2e08(&hi, &lo, &prev, rc, &res) && res) return 1;
        }
        prev = *rc; dl = bl;
    }
    return 0;
}

typedef struct {
    R9 *seg_s, *seg_e;     /* local_34 / local_3c (tag byte of seg_s = unused flag) */
    int ns;
    R9 *obuf; int ocnt;    /* output buffer P-0x2474.. (index from 1), count G-0x10 */
    R9s *out;
    R9 cs, ce;             /* current segment start / end (G-0x20 / G-0x2c) */
} HLink;

/* FUN_004f3094 (l.45197-45223): rotate records 1..cnt-1 back by +angle and write them. */
static int flush_4f3094(HCore *h, HLink *L)
{
    for (int k = 1; k < L->ocnt; k++) {
        R9 r = L->obuf[k];
        rot_4f24ec(&r.x, &r.y, h->b->angle);
        if (r9_push(L->out, r.tag, r.x, r.y)) return EG_ROUGH_ENOMEM;
    }
    L->ocnt = 1;
    return 0;
}

static int obuf_add(HLink *L, unsigned char tag, float x, float y)
{
    /* the original buffer holds 1000 records and is flushed at > 997 / > 998 entries */
    L->obuf[L->ocnt].tag = tag; L->obuf[L->ocnt].x = x; L->obuf[L->ocnt].y = y;
    L->ocnt++;
    return 0;
}

/* FUN_004f3334 (l.45321-45573, @4f3334): link the row segments of HELP.TMP into zigzag paths.
 * Segment = (last non-0x0a record, 0x0a record).  For every unused segment i (file order):
 * write 0x0b start, 0x0a end, then repeatedly take the unused segment j > i whose start lies
 * strictly within +-1.9 s in y of the current end and is nearest (d^2 < (36 s)^2, strict <, first
 * minimum); join it with a straight 0x0a when the joint does not cross the boundary
 * (FUN_004f2f20), else try FUN_004f3100; stop at the first refused joint. */
static int link_4f3334(HCore *h, const R9s *help, R9s *out)
{
    HLink L; memset(&L, 0, sizeof L);
    int rc = 0;
    L.out = out;
    if (help->n <= 0) return 0;
    L.seg_s = (R9 *)malloc(sizeof(R9) * (size_t)(help->n + 1));
    L.seg_e = (R9 *)malloc(sizeof(R9) * (size_t)(help->n + 1));
    L.obuf  = (R9 *)malloc(sizeof(R9) * 1010);
    if (!L.seg_s || !L.seg_e || !L.obuf) { rc = EG_ROUGH_ENOMEM; goto done; }
    R9 st = help->r[0];                                    /* l.45377: first record read alone */
    for (int i = 1; i < help->n; i++) {                    /* l.45382-45422 */
        const R9 *p = &help->r[i];
        if (p->tag == 0x0a) {
            L.seg_s[L.ns] = st; L.seg_s[L.ns].tag = 1;
            L.seg_e[L.ns] = *p;
            L.ns++;
            if (L.ns > 45000) { *h->err = EG_HATCH_ERR_SEGS; goto done; }
        } else st = *p;
    }
    if (L.ns > 0) {
        L.ocnt = 1;
        for (int i = 0; i < L.ns; i++) {                   /* l.45436 */
            if (!L.seg_s[i].tag) continue;
            obuf_add(&L, 0x0b, L.seg_s[i].x, L.seg_s[i].y);
            L.seg_s[i].tag = 0;
            obuf_add(&L, 0x0a, L.seg_e[i].x, L.seg_e[i].y);
            L.cs = L.seg_s[i]; L.ce = L.seg_e[i];
            if (i < L.ns - 1) {                            /* @4f3560 */
                int crossed = 0, jb;
                do {
                    float best = h->far2;                  /* @4f356c */
                    jb = 0;
                    float hi = F((double)L.ce.y + h->k19), lo = F((double)L.ce.y - h->k19);
                    for (int j = i + 1; j < L.ns; j++) {   /* @4f359d */
                        const R9 *c = &L.seg_s[j];
                        if (!c->tag) continue;
                        if (!(c->y < hi) || !(c->y > lo)) continue;   /* @4f35b7 / @4f35c3 */
                        double dx = (double)c->x - L.ce.x, dy = (double)c->y - L.ce.y;
                        float d2 = F(dx * dx + dy * dy);
                        if (d2 < best) { best = d2; jb = j; }
                    }
                    if (jb > 0) {
                        crossed = joint_4f2f20(h, &L.ce, &L.seg_s[jb]);
                        if (!crossed) {                    /* l.45510-45539 */
                            obuf_add(&L, 0x0a, L.seg_s[jb].x, L.seg_s[jb].y);
                            L.seg_s[jb].tag = 0;
                            obuf_add(&L, 0x0a, L.seg_e[jb].x, L.seg_e[jb].y);
                            L.cs = L.seg_s[jb]; L.ce = L.seg_e[jb];
                        } else {
                            /* FUN_004f3100 (l.45226-45318, @4f3100): if the candidate start x lies
                             * within the x span of the current segment, go back along the current
                             * row to Q = (start.x, end.y) and drop straight to the start. */
                            float lx = L.cs.x <= L.ce.x ? L.cs.x : L.ce.x;
                            float hx = L.cs.x <= L.ce.x ? L.ce.x : L.cs.x;
                            const R9 *c = &L.seg_s[jb];
                            if (!(c->x < lx) && !(c->x > hx)) {
                                R9 q; q.tag = 0x0a; q.x = c->x; q.y = L.ce.y;
                                crossed = joint_4f2f20(h, &q, c);
                                if (!crossed) {
                                    obuf_add(&L, 0x0a, q.x, q.y);
                                    if (L.ocnt > 0x3e6 && (rc = flush_4f3094(h, &L))) goto done;
                                    obuf_add(&L, 0x0a, c->x, c->y);
                                    L.seg_s[jb].tag = 0;
                                    obuf_add(&L, 0x0a, L.seg_e[jb].x, L.seg_e[jb].y);
                                    L.cs = L.seg_s[jb]; L.ce = L.seg_e[jb];
                                }
                            }
                        }
                    }
                    if (L.ocnt > 0x3e5 && (rc = flush_4f3094(h, &L))) goto done;
                    /* user abort check (FUN_004bc3c0) not ported */
                } while (!crossed && jb != 0 && *h->err == 0);
            }
            if (*h->err) break;
        }
        if (L.ocnt > 1) rc = flush_4f3094(h, &L);
    }
done:
    free(L.seg_s); free(L.seg_e); free(L.obuf);
    return rc;
}

/* FUN_004f378c (l.45576-45625, const 0.01 @4f38a4): box of the 0x0a / 0x0c records, +-0.01. */
static void box_4f378c(const R9s *f, float *xmin, float *ymin, float *xmax, float *ymax)
{
    *xmin = 10000.0f; *ymin = 10000.0f; *xmax = -10000.0f; *ymax = -10000.0f;
    for (int i = 0; i < f->n; i++) {
        const R9 *p = &f->r[i];
        if (p->tag != 0x0a && p->tag != 0x0c) continue;
        if (p->x < *xmin) *xmin = p->x;
        if (*xmax < p->x) *xmax = p->x;
        if (p->y < *ymin) *ymin = p->y;
        if (*ymax < p->y) *ymax = p->y;
    }
    *xmin = F(*xmin - 0.01); *ymin = F(*ymin - 0.01);
    *xmax = F(0.01 + *xmax); *ymax = F(0.01 + *ymax);
}

/* FUN_004f39e0 (l.45673-45999, @4f39e0): the hatch core.  bnd = VekH2R0 (9-byte, modified in
 * place by the rotation / rectangle append), out = VekH2R1. */
static int hatch_4f39e0(R9s *bnd, R9s *out, unsigned char *err, HB *b)
{
    HCore h; memset(&h, 0, sizeof h);
    int rc = 0;
    R9s help = {0, 0, 0};
    h.b = b; h.err = err;
    *err = 0;
    int n = bnd->n;                                                   /* l.45775 */
    if (n < 1) { *err = EG_HATCH_ERR_EMPTY; return 0; }
    if (!(6.283185307179586 > fabs((double)b->angle))) *err = EG_HATCH_ERR_ANGLE;  /* @4f3aba */
    if (b->e < 0.0f) b->e = 0.0f;                                     /* @4f3ad1 */
    if (*err) return 0;
    float fx0, fy0, fx1, fy1;
    box_4f378c(bnd, &fx0, &fy0, &fx1, &fy1);
    if (b->flag) { b->xmin = fx0; b->xmax = fx1; b->ymin = fy0; b->ymax = fy1; }   /* @4f3b04 */
    else if ((0.01 + fx0) < b->xmin || (fx1 - 0.01) > b->xmax ||
             (fy1 - 0.01) > b->ymax || (0.01 + fy0) < b->ymin)       /* @4f3b35-4f3b92 */
        *err = EG_HATCH_ERR_BOX;
    if (b->angle != 0.0f) {                                           /* l.45805-45861 */
        R9 rect[5] = { {0x0b, b->xmin, b->ymin}, {0x0a, b->xmax, b->ymin}, {0x0a, b->xmax, b->ymax},
                       {0x0a, b->xmin, b->ymax}, {0x0a, b->xmin, b->ymin} };
        if (!b->flag) {
            for (int k = 0; k < 5; k++)
                if (r9_push(bnd, rect[k].tag, rect[k].x, rect[k].y)) return EG_ROUGH_ENOMEM;
            n += 5;
            b->flag = 1;
        }
        b->xmin = 10000.0f; b->ymin = 10000.0f; b->xmax = -10000.0f; b->ymax = -10000.0f;
        for (int k = 1; k < 5; k++) {
            rot_4f24ec(&rect[k].x, &rect[k].y, -b->angle);
            if (rect[k].x < b->xmin) b->xmin = rect[k].x;
            if (rect[k].y < b->ymin) b->ymin = rect[k].y;
            if (b->xmax < rect[k].x) b->xmax = rect[k].x;
            if (b->ymax < rect[k].y) b->ymax = rect[k].y;
        }
        b->xmin = F(b->xmin - 0.01); b->xmax = F(0.01 + b->xmax);
        b->ymin = F(b->ymin - 0.01); b->ymax = F(0.01 + b->ymax);
        for (int i = 0; i < bnd->n; i++) rot_4f24ec(&bnd->r[i].x, &bnd->r[i].y, -b->angle); /* FUN_004f391c */
    }
    h.s20 = F(b->s / 20.0);
    h.far2 = F(((double)b->s * 36.0) * ((double)b->s * 36.0));
    h.k19 = F(1.9 * (double)b->s);
    if (*err) return 0;

    h.xs = (XI *)malloc(sizeof(XI) * 30001);
    h.buf = (R9 *)malloc(sizeof(R9) * 30001);
    if (!h.xs || !h.buf) { rc = EG_ROUGH_ENOMEM; goto done; }
    int band = 6000;                                                  /* @4f3e27 */
    float hband = F(((double)b->ymax - b->ymin) / ((double)n / (double)band));
    h.stripLow = b->ymax;                                             /* @4f3e60 */
    h.Y = (double)b->ymax - (double)b->s / 8.0;                       /* @4f3e6c */
    do {
        float top;
        if (n < 15000) {                                              /* @4f3e97 */
            memcpy(h.buf, bnd->r, sizeof(R9) * (size_t)n);
            h.cnt = n;
            top = b->ymax; h.stripLow = b->ymin;
        } else {                                                      /* @4f3ee2: banded */
            top = h.stripLow;
            h.stripLow = F((double)top - hband);
            R9 prev; prev.tag = 0; prev.x = 0.0f; prev.y = 0.0f;     /* tag not reset in orig. */
            unsigned char prevOut = 0, pend = 0;
            h.cnt = 0;
            for (int i = 0; i < n; i++) {
                const R9 *rc9 = &bnd->r[i];
                unsigned char o = (rc9->y > top || rc9->y < h.stripLow) ? 1 : 0;
                int add = 1;
                if (prevOut & o) {
                    float d = F((double)rc9->y - prev.y);
                    if (d == 0.0f) { pend = 1; add = 0; }
                    else {
                        float t = F(((double)top - prev.y) / d);
                        if (t < 0.0f || t > 1.0f) { pend = 1; add = 0; }
                    }
                }
                if (add) {                                            /* FUN_004f243c l.44590 */
                    if (h.cnt < 30000) {
                        if (pend) { h.buf[h.cnt++] = prev; pend = 0; }
                        if (h.cnt < 30001) h.buf[h.cnt] = *rc9;
                    }
                    h.cnt++;
                }
                prev = *rc9; prevOut = o;
            }
        }
        if (h.cnt < 30000) {                                          /* @4f407d */
            if (h.stripLow < b->ymin) h.stripLow = b->ymin;
            if (b->link) {
                help.n = 0;
                if ((rc = rows_4f2a80(&h, &help, 0))) goto done;
                if ((rc = link_4f3334(&h, &help, out))) goto done;
            } else {
                if ((rc = rows_4f2a80(&h, out, b->oneway))) goto done;
            }
        } else {                                                      /* @4f4132 */
            h.stripLow = top;
            band /= 2;
            hband = F(((double)b->ymax - b->ymin) / ((double)n / (double)band));
            if (band < 0x2ee) *err = EG_HATCH_ERR_BAND;
        }
    } while (h.stripLow > b->ymin && *err == 0);                       /* @4f4193 */
done:
    free(h.xs); free(h.buf); free(help.r);
    return rc;
}

/* FUN_00510438 (l.68955-69052, @510438): 13 -> 9 byte copy + box of the 0x0a / 0x0c records. */
static void box_510438(const EgRecs *f, float *xmin, float *ymax, float *xmax, float *ymin)
{
    *xmin = 10000.0f; *ymin = 10000.0f; *xmax = -10000.0f; *ymax = -10000.0f;
    for (int i = 0; i < f->n; i++) {
        if (f->r[i].tag != 0x0a && f->r[i].tag != 0x0c) continue;
        float x = F(f->r[i].x), y = F(f->r[i].y);
        if (x < *xmin) *xmin = x;
        if (*xmax < x) *xmax = x;
        if (y < *ymin) *ymin = y;
        if (*ymax < y) *ymax = y;
    }
}

/* common box set-up of FUN_0051064c l.69118-69143 / FUN_005108d4 l.69246-69271 */
static void box_setup(const EgRecs *bnd, const EgBox *g, float s, HB *b)
{
    float xmin, ymax, xmax, ymin;
    box_510438(bnd, &xmin, &ymax, &xmax, &ymin);
    float q = F(s / 4.0);
    b->xmin = (g->xmin <= F(xmin - q)) ? g->xmin : F(xmin - q);
    b->ymin = (g->ymin <= F(ymin - q)) ? g->ymin : F(ymin - q);
    b->xmax = (F(xmax + q) <= g->xmax) ? g->xmax : F(xmax + q);
    b->ymax = (F(ymax + q) <= g->ymax) ? g->ymax : F(ymax + q);
}

/* FUN_0051064c (l.69055-69174, @51064c) - 'Z' hatch.  FUN_004c48c4 (l.15352) = file size;
 * nothing happens for <= 0x27 bytes (3 records). */
int eg_rough_hatch_zigzag_51064c(const EgRecs *boundary, const EgRecs *frame, int gravur_v,
                                 const EgBox *gbox, float angle, float spacing, float e_stack,
                                 EgRecs *out, int *eng_err)
{
    int rc;
    *eng_err = 0;
    if (!boundary || !gbox || !out) return EG_ROUGH_EARG;
    if ((rc = recs_copy(boundary, out))) return rc;        /* VekAR10 unchanged unless replaced */
    if (boundary->n * 13 <= 0x27) return EG_ROUGH_OK;
    HB b; memset(&b, 0, sizeof b);
    box_setup(boundary, gbox, spacing, &b);
    b.s = spacing;
    b.e = e_stack;
    if (b.e < F(spacing / 4.0)) b.e = F(spacing / 4.0);    /* @510797: uninitialised local, max */
    b.angle = angle;
    b.flag = gravur_v ? 1 : 0;
    b.link = 1;                                            /* @5107d5 */
    b.oneway = 0;                                          /* @5107dc */
    R9s h0 = {0, 0, 0}, h1 = {0, 0, 0};
    if ((rc = conv_13_to_9(boundary, &h0))) goto done;
    int has_frame = frame && frame->n > 0;
    if (b.flag == 0 && has_frame) {                        /* l.69156 */
        if ((rc = conv_13_to_9(frame, &h0))) goto done;
        b.flag = 1;
    }
    unsigned char err = 0;
    if ((rc = hatch_4f39e0(&h0, &h1, &err, &b))) goto done;
    if (err) { *eng_err = err + 0x14; goto done; }         /* l.69161 */
    if (has_frame && 0.01 < fabs((double)angle))           /* l.69165 */
        if ((rc = conv_13_to_9(frame, &h1))) goto done;
    out->n = 0;
    rc = conv_9_to_13(&h1, out);                           /* l.69169 FUN_004f5348(.., 0) */
done:
    free(h0.r); free(h1.r);
    return rc;
}

/* FUN_005108d4 (l.69177-69297) - 'Y': same box, inset e = s/4, no HELP linking, rows always
 * from xmin to xmax (+0x1f = 1), output appended to file(10) (FUN_004f5348(.., 1)). */
int eg_rough_hatch_oneway_5108d4(const EgRecs *boundary, const EgRecs *frame, int gravur_v,
                                 const EgBox *gbox, float angle, float spacing,
                                 EgRecs *tool10_append, int *eng_err)
{
    int rc = 0;
    *eng_err = 0;
    if (!boundary || !gbox || !tool10_append) return EG_ROUGH_EARG;
    if (boundary->n * 13 <= 0x27) return EG_ROUGH_OK;
    HB b; memset(&b, 0, sizeof b);
    box_setup(boundary, gbox, spacing, &b);
    b.s = spacing; b.e = F(spacing / 4.0); b.angle = angle;
    b.flag = gravur_v ? 1 : 0; b.link = 0; b.oneway = 1;   /* l.69276-69281 */
    R9s h0 = {0, 0, 0}, h1 = {0, 0, 0};
    if ((rc = conv_13_to_9(boundary, &h0))) goto done;
    if (b.flag == 0 && frame && frame->n > 0) {
        if ((rc = conv_13_to_9(frame, &h0))) goto done;
        b.flag = 1;
    }
    unsigned char err = 0;
    if ((rc = hatch_4f39e0(&h0, &h1, &err, &b))) goto done;
    if (err) { *eng_err = err + 0x14; goto done; }
    rc = conv_9_to_13(&h1, tool10_append);
done:
    free(h0.r); free(h1.r);
    return rc;
}

/* ============================== merge FUN_0051170c / FUN_00511310 ============================== */

typedef struct {
    int first, last;          /* +4 / +8  */
    float sx, sy, ex, ey;     /* +0x10.. / +0x18.. */
    float thr;                /* +0x34 */
    unsigned char zflag;      /* +0x38 */
} SubP;

/* FUN_00510c6c (l.69417-69555, const 0.7 @510e05): index of the 0x0b-started subpaths.
 * Returns the index of the last subpath (-1 none), sets *short_err 0x44f for < 4 records. */
static int index_510c6c(const EgRecs *f, float thr0, SubP **out, int *short_err)
{
    int cap = 64, cnt = 0, started = 0;
    SubP *a = (SubP *)calloc((size_t)cap, sizeof(SubP));
    if (!a) return -2;
    float thr = thr0;
    float lx = 0.0f, ly = 0.0f;
    *short_err = 0;
    if (f->n < 4) { *short_err = 0x44f; *out = a; return -1; }
    a[0].thr = thr;
    for (int i = 0; i < f->n; i++) {
        const EgRec *p = &f->r[i];
        if (p->tag == 0x0b) {
            if (started) {
                a[cnt].last = i - 1;
                a[cnt].thr = thr;                             /* l.69477 */
                if (a[cnt].first < i - 1) {                   /* l.69479 */
                    a[cnt].ex = lx; a[cnt].ey = ly;
                    cnt++;
                    if (cnt >= cap) {
                        SubP *q = (SubP *)realloc(a, sizeof(SubP) * (size_t)cap * 2);
                        if (!q) { free(a); return -2; }
                        memset(q + cap, 0, sizeof(SubP) * (size_t)cap);
                        a = q; cap *= 2;
                    }
                }
            }
            started = 1;
            a[cnt].thr = thr; a[cnt].first = i;
            a[cnt].sx = F(p->x); a[cnt].sy = F(p->y);
            long zr = (long)rint(p->z);                        /* l.69499 Round(z) */
            a[cnt].zflag = zr == -5 ? 0xfb : zr == -4 ? 0xfc : 0;
        } else if (p->tag == 0x80) {
            if (p->z > 0.0) thr = F(0.7 * p->z);              /* l.69510-69513 */
        } else { lx = F(p->x); ly = F(p->y); }
    }
    a[cnt].last = f->n - 1;
    a[cnt].ex = lx; a[cnt].ey = ly;
    if (!(a[cnt].first < f->n - 1)) cnt--;                     /* l.69542 */
    *out = a;
    return cnt;
}

/* FUN_00511194 (l.69705-69769, @511194, consts -0.001 / 1.001): does the joint (cx,cy)->(sx,sy)
 * cross a segment of the finishing path (0x0b resets, 0x0a / 0x0c are line ends)? */
static int cross_fin_511194(const EgRecs *fin, float cx, float cy, float sx, float sy)
{
    if (!fin || fin->n < 2) return 0;
    float L8 = F((double)sx - cx), Lc = F((double)sy - cy);
    float px = F(fin->r[0].x), py = F(fin->r[0].y);
    for (int i = 1; i < fin->n; i++) {
        const EgRec *q = &fin->r[i];
        if (q->tag == 0x0b) { px = F(q->x); py = F(q->y); continue; }
        if (q->tag != 0x0a && q->tag != 0x0c) continue;
        float qx = F(q->x), qy = F(q->y);
        float d10 = F((double)px - qx), d14 = F((double)py - qy);
        float den = F((double)Lc * d10 - (double)L8 * d14);
        if (den != 0.0f) {
            float t1 = F((((double)py - cy) * d10 - ((double)px - cx) * d14) / den);
            if (!(-0.001 > t1) && !(1.001 < t1)) {
                float t2 = F((((double)px - cx) * Lc - ((double)py - cy) * L8) / den);
                if (!(-0.001 > t2) && !(1.001 < t2)) return 1;
            }
        }
        px = qx; py = qy;
    }
    return 0;
}

/* FUN_00510f84 (l.69585-69674): copy records first..last (tags 0x0a..0x0c) to the tool file;
 * with connect the leading record becomes 0x0a when the next record is 0x0a / 0x0b, else it is
 * dropped. */
static int write_510f84(const EgRecs *f, const SubP *sp, int connect, EgRecs *dst)
{
    int pending = connect;
    for (int i = sp->first; i <= sp->last; i++) {
        const EgRec *p = &f->r[i];
        if (p->tag < 0x0a || p->tag > 0x0c) continue;
        if (!pending) {
            if (eg_recs_push(dst, p->tag, p->x, p->y, p->z)) return EG_ROUGH_ENOMEM;
        } else {
            pending = 0;
            int nt = (i + 1 < f->n) ? f->r[i + 1].tag : 0;
            if (nt == 0x0a || nt == 0x0b)
                if (eg_recs_push(dst, 0x0a, p->x, p->y, p->z)) return EG_ROUGH_ENOMEM;
        }
    }
    return EG_ROUGH_OK;
}

/* FUN_0051170c (l.69953-69991) -> FUN_00511310 (l.69772-69950, @511310). */
int eg_rough_merge_51170c(const EgRecs *hatch, const EgRecs *finish, int connect, int kzv,
                          float spacing, EgRecs *tool10_append, int *short_err)
{
    SubP *sp = NULL;
    int rc = EG_ROUGH_OK;
    *short_err = 0;
    if (!hatch || !tool10_append) return EG_ROUGH_EARG;
    if (hatch->n < 1) { *short_err = 0x44f; return EG_ROUGH_OK; }    /* FUN_00510f10 l.69572 */
    float thr0 = F(0.7 * (double)spacing);                            /* @511365 */
    int last = index_510c6c(hatch, thr0, &sp, short_err);
    if (last == -2) return EG_ROUGH_ENOMEM;
    if (*short_err >= 2) { free(sp); return EG_ROUGH_OK; }            /* @5113fe */
    unsigned char *used = (unsigned char *)calloc((size_t)(last + 2), 1);
    if (!used) { free(sp); return EG_ROUGH_ENOMEM; }
    int cur = connect ? 0 : last;                                     /* @511480 */
    float cx = 0.0f, cy = 0.0f;
    if (last >= 0) { cx = sp[cur].sx; cy = sp[cur].sy; }
    int first = 1;
    for (;;) {
        int done = 1, bi = 0;
        float best = 1e20f;                                           /* @5114c7 */
        for (int k = 0; k <= last; k++) {                             /* FUN_005110dc l.69677 */
            int idx = connect ? k : last - k;
            if (used[idx]) continue;
            done = 0;
            double dx = (double)cx - sp[idx].sx, dy = (double)cy - sp[idx].sy;
            float d = F(sqrt(dx * dx + dy * dy));
            if (d < best) { best = d; bi = idx; }
        }
        if (!done) {
            const SubP *b = &sp[bi];
            if (first || ((best < b->thr || kzv) &&
                          !cross_fin_511194(finish, cx, cy, b->sx, b->sy))) {   /* @511543-511588 */
                if ((rc = write_510f84(hatch, b, !first, tool10_append))) break;
                first = 0;
                used[bi] = 0xff;
                cx = b->ex; cy = b->ey;
            } else {                                                  /* @5115e3 */
                first = 1;
                for (int k = 0; k <= last; k++) {
                    int idx = connect ? k : last - k;
                    if (!used[idx]) { cx = sp[idx].sx; cy = sp[idx].sy; break; }
                }
            }
        } else break;
    }
    free(used); free(sp);
    return rc;
}

/* FUN_00527dfc (l.86594-86687): append the 0x0a..0x0c records of src to dst. */
int eg_rough_append_527dfc(const EgRecs *src, EgRecs *dst)
{
    if (!src || !dst) return EG_ROUGH_EARG;
    for (int i = 0; i < src->n; i++)
        if (src->r[i].tag >= 0x0a && src->r[i].tag <= 0x0c)
            if (eg_recs_push(dst, src->r[i].tag, src->r[i].x, src->r[i].y, src->r[i].z)) return EG_ROUGH_ENOMEM;
    return EG_ROUGH_OK;
}

/* ============================== corner rounding FUN_00513e38 ============================== */

typedef struct { int tag; float x, y, z; } R13;
typedef struct {
    EgRecs *out;
    R13 last;     /* F-0x160: last emitted record (dedup) */
    R13 prev;     /* F-0x180 */
    R13 cur;      /* F-0x170 */
    int nseg;     /* F-0x182 */
    float d, d2;  /* F+8, F-0x188 = 2 d */
} Rnd;

/* FUN_0051380c (l.71616-71662, eps 0.001 @51382e): append unless within 0.001 of the last. */
static int emit_51380c(Rnd *g, int tag, float x, float y, float z)
{
    if (g->last.tag == 0 || fabs((double)g->last.x - x) > 0.001 || fabs((double)g->last.y - y) > 0.001) {
        R13 r; r.tag = tag; r.x = x; r.y = y;
        r.z = (tag == 0x0a || tag == 0x0b) ? 0.0f : z;               /* l.71641-71649 */
        if (eg_recs_push(g->out, r.tag, r.x, r.y, r.z)) return EG_ROUGH_ENOMEM;
        g->last = r;
    }
    return 0;
}

/* FUN_00513944 (l.71692): flush the pending point, reset prev / cur. */
static int flush_513944(Rnd *g)
{
    int rc = 0;
    if (g->cur.tag) rc = emit_51380c(g, g->cur.tag, g->cur.x, g->cur.y, g->cur.z);
    g->prev.tag = 0; g->cur.tag = 0;
    return rc;
}

/* FUN_00513b7c (l.71770-71858) + FUN_00513994 (l.71707-71767): replace the corner cur between
 * prev and n by a quadratic Bezier A -> B with control point cur; A / B lie d before / after the
 * corner (midpoints when the leg is shorter than 2 d); nseg segments. */
static int round_513b7c(Rnd *g, const R13 *n)
{
    int tag = (g->prev.tag == 0x0c || g->cur.tag == 0x0c || n->tag == 0x0c) ? 0x0c : 0x0a;
    float v1x = F((double)g->cur.x - g->prev.x), v1y = F((double)g->cur.y - g->prev.y), v1z = F((double)g->cur.z - g->prev.z);
    float v2x = F((double)n->x - g->cur.x), v2y = F((double)n->y - g->cur.y), v2z = F((double)n->z - g->cur.z);
    float L1 = F(sqrt((double)v1x * v1x + (double)v1y * v1y + (double)v1z * v1z));
    float L2 = F(sqrt((double)v2x * v2x + (double)v2y * v2y + (double)v2z * v2z));
    R13 A, B; int c1, c2, nn;
    if (g->d2 <= L1) {
        float k = F(L1 / g->d);
        A.x = F(g->cur.x - v1x / k); A.y = F(g->cur.y - v1y / k); A.z = F(g->cur.z - v1z / k); c1 = 4;
    } else {
        A.x = F((g->prev.x + g->cur.x) / 2.0); A.y = F((g->prev.y + g->cur.y) / 2.0);
        A.z = F((g->prev.z + g->cur.z) / 2.0); c1 = (int)rint(L1 / g->d) * 3;   /* Delphi Round = banker's */
    }
    if (g->d2 <= L2) {
        float k = F(L2 / g->d);
        B.x = F(v2x / k + g->cur.x); B.y = F(v2y / k + g->cur.y); B.z = F(v2z / k + g->cur.z); c2 = 4;
    } else {
        B.x = F((g->cur.x + n->x) / 2.0); B.y = F((g->cur.y + n->y) / 2.0);
        B.z = F((g->cur.z + n->z) / 2.0); c2 = (int)rint(L2 / g->d) * 3;
    }
    if (g->nseg < 2) { nn = c2 < c1 ? c1 : c2; if (nn < 2) nn = 2; }
    else if (g->nseg - 2 < 999) nn = g->nseg;
    else return EG_ROUGH_NOT_DECODED;            /* local_c uninitialised in the original */
    int rc;
    if ((rc = emit_51380c(g, tag, A.x, A.y, A.z))) return rc;
    if (g->nseg < 0) {                                       /* l.71727-71737 */
        if ((rc = emit_51380c(g, tag, F(B.x * 0.25 + g->cur.x * 0.5 + A.x * 0.25),
                              F(B.y * 0.25 + g->cur.y * 0.5 + A.y * 0.25),
                              F(B.z * 0.25 + g->cur.z * 0.5 + A.z * 0.25)))) return rc;
        return emit_51380c(g, tag, B.x, B.y, B.z);
    }
    for (int k = 1; k <= nn; k++) {
        float t = F((float)k / (float)nn), u = F(1.0f - t);
        float c0 = F(u * u), cm = F(u * 2.0f * t), cc = F(t * t);
        if ((rc = emit_51380c(g, tag, F(cc * B.x + cm * g->cur.x + c0 * A.x),
                              F(cc * B.y + cm * g->cur.y + c0 * A.y),
                              F(cc * B.z + cm * g->cur.z + c0 * A.z)))) return rc;
    }
    return 0;
}

/* FUN_00513e38 (l.71861-72088). */
int eg_rough_round_513e38(const EgRecs *in, int nseg, float d, EgRecs *out)
{
    if (!in || !out) return EG_ROUGH_EARG;
    Rnd g; memset(&g, 0, sizeof g);
    g.out = out; out->n = 0;
    g.nseg = nseg > 8 ? 8 : nseg;                            /* l.71929 */
    g.d = d; g.d2 = F(d * 2.0f);                              /* l.71942 */
    float zsrc = 0.0f;                                        /* local_190 */
    int rc;
    for (int i = 0; i < in->n; i++) {
        R13 r; r.tag = in->r[i].tag; r.x = F(in->r[i].x); r.y = F(in->r[i].y); r.z = F(in->r[i].z);
        if (r.tag == 0x0c || r.tag == 0x12 || r.tag == 0x0a) {
            if (r.tag == 0x0a) r.z = zsrc;                    /* l.71995 */
            if (g.prev.tag == 0 || g.prev.tag == 0x0b || g.cur.tag == 0) {
                if (g.cur.tag && (rc = emit_51380c(&g, g.cur.tag, g.cur.x, g.cur.y, g.cur.z))) return rc;
            } else if ((rc = round_513b7c(&g, &r))) return rc;
            g.prev = g.cur; g.cur = r;
        } else if (r.tag == 0x0b) {
            if ((rc = flush_513944(&g))) return rc;
            g.cur = r;
        } else if (r.tag == 0x9c) {                           /* l.72054-72060 */
            zsrc = r.y;
            if (eg_recs_push(out, r.tag, in->r[i].x, in->r[i].y, in->r[i].z)) return EG_ROUGH_ENOMEM;
        } else {                                              /* LAB_005140d4 */
            if ((rc = flush_513944(&g))) return rc;
            if (eg_recs_push(out, r.tag, in->r[i].x, in->r[i].y, in->r[i].z)) return EG_ROUGH_ENOMEM;
        }
    }
    return flush_513944(&g);                                  /* l.72077 */
}

/* ============================== driver FUN_00528104 'Z' / 'Y' ============================== */

int eg_rough_528104(const EgRoughParams *p, const EgRecs *boundary, const EgRecs *frame,
                    EgRecs *finish, EgRecs *tool10, int *eng_err)
{
    int rc = EG_ROUGH_OK;
    *eng_err = 0;
    if (!p || !boundary || !finish || !tool10) return EG_ROUGH_EARG;
    /* spacing = (2 - overlap/50) * R[slot].rt  (@528431-528456) */
    float spacing = F((2.0 - (double)p->overlap / 50.0) * p->rt);
    if (p->verf == 'Z') {                                     /* l.86833-86843 */
        EgRecs ar10 = {0, 0, 0};
        int se = 0;
        rc = eg_rough_hatch_zigzag_51064c(boundary, frame, p->gravur_v, &p->gbox, p->angle,
                                          spacing, p->e_stack, &ar10, eng_err);
        if (!rc) rc = eg_rough_merge_51170c(&ar10, finish, 1, p->kzv, spacing, tool10, &se);
        eg_recs_free(&ar10);
        if (rc) return rc;
    } else if (p->verf == 'Y') {                              /* l.86825-86831 */
        rc = eg_rough_hatch_oneway_5108d4(boundary, frame, p->gravur_v, &p->gbox, p->angle,
                                          spacing, tool10, eng_err);
        if (rc) return rc;
    } else return EG_ROUGH_EARG;
    if (p->abr_weg_zus) {                                     /* l.86845-86862 */
        if (!p->abr_zuerst) {
            if ((rc = eg_rough_append_527dfc(tool10, finish))) return rc;
            tool10->n = 0;                                    /* FUN_004c4720: rewrite empty */
        } else {
            if ((rc = eg_rough_append_527dfc(finish, tool10))) return rc;
            finish->n = 0;
        }
    }
    if (*eng_err == 100) *eng_err = 0;                        /* l.86863 */
    /* FUN_0052919c tail (l.87399-87407): 'Z', KAbrZeilenRund, !KAbrWegZusammen; runs after
     * FUN_00528688 (re-sort, not ported) on the whole file(10). */
    if (p->verf == 'Z' && p->abr_zeilen_rund && !p->abr_weg_zus && *eng_err < 1) {
        EgRecs r = {0, 0, 0};
        float d = F((double)spacing / 2.5);                   /* @5293f7-52941f */
        rc = eg_rough_round_513e38(tool10, 5, d, &r);
        if (!rc) rc = recs_copy(&r, tool10);
        eg_recs_free(&r);
    }
    return rc;
}

#undef HB
#undef HCore
#undef HLink
#undef HRow
#undef R13
#undef R9
#undef R9s
#undef Rnd
#undef SubP
#undef XI
#undef box_4f378c
#undef box_510438
#undef box_setup
#undef conv_13_to_9
#undef conv_9_to_13
#undef cross_4f2e08
#undef cross_fin_511194
#undef eg_recs_free
#undef eg_recs_push
#undef eg_recs_push_rec
#undef emit_51380c
#undef flush_4f3094
#undef flush_513944
#undef gate_4f2a44
#undef hatch_4f39e0
#undef index_510c6c
#undef joint_4f2f20
#undef link_4f3334
#undef mv_4f2644
#undef obuf_add
#undef r9_push
#undef recs_copy
#undef rot_4f24ec
#undef round_513b7c
#undef rowend_4f29e8
#undef rows_4f2a80
#undef write_510f84
#undef x_4f27b0
#undef F
#undef EgBox
typedef rgh_EgBox EgRBox;

/* ================================================================================================================== */
/* the pipeline: input, tool rows, contour preparation, orientation, blocks, re-sort, depth cuts, LG1 (formats/engrave_engine.md 2, 7)
   (port of eg_pipe.c, decoded from EStamp7.exe; renamed file-scope names: pip_*) */
#define BlkSt pip_BlkSt
#define Cell pip_Cell
#define F pip_F
#define LoadSt pip_LoadSt
#define OvSt pip_OvSt
#define Seg9 pip_Seg9
#define Spike pip_Spike
#define block_cell pip_block_cell
#define copy_contour pip_copy_contour
#define corner_metric pip_corner_metric
#define dist2d pip_dist2d
#define eg_recs_free pip_eg_recs_free
#define eg_recs_push pip_eg_recs_push
#define eg_recs_push_rec pip_eg_recs_push_rec
#define eg_recs_reserve pip_eg_recs_reserve
#define field_is_int pip_field_is_int
#define layer_pass pip_layer_pass
#define le_word pip_le_word
#define link_collides pip_link_collides
#define link_len_add pip_link_len_add
#define load_emit pip_load_emit
#define load_flush_pend pip_load_flush_pend
#define outcode pip_outcode
#define ov_mark pip_ov_mark
#define parity_inside pip_parity_inside
#define prep_flush pip_prep_flush
#define probe_x pip_probe_x
#define rd32 pip_rd32
#define reduce_contour pip_reduce_contour
#define resort_variant pip_resort_variant
#define reverse_one pip_reverse_one
#define rotate_out pip_rotate_out
#define seed_contains pip_seed_contains
#define slot_ok pip_slot_ok
#define spike_connected pip_spike_connected
#define start_one pip_start_one
#define tan_f pip_tan_f
#define wr32 pip_wr32
#define write_pass pip_write_pass
/* eg_pipe.h - EStamp7.exe (HCAM / AHSoft engrave engine) pipeline around the geometry kernels.
 *
 * Port of the decoded pipeline stages of EStamp7.exe (32-bit Delphi). Every function names the
 * binary function it ports; NOTES.md gives the dump-line citations. Stages that are NOT DECODED
 * return EG_ERR_NOT_DECODED instead of inventing behaviour.
 *
 * Units: mm. Records: the LG1 13-byte layout (tag u8 + 3 x f32). Coordinates are kept as
 * double but every value the binary stores to an f32 slot is rounded to float here as well.
 */



/* ---- record tags (LG1) ---- */
enum {
    EG_T_END    = 0x09, /* end of a tool block (FUN_0050cddc) */
    EG_T_LINE   = 0x0a, /* line-to */
    EG_T_MOVE   = 0x0b, /* start of a contour / move-to */
    EG_T_POINT  = 0x0c, /* 3D point (line-to with z) */
    EG_T_ARC    = 0x0d,
    EG_T_HDR1   = 0x01, /* tool block header: x=xmin y=ymin z=int record count */
    EG_T_HDR5   = 0x05, /* x=xmax y=ymax z=xmin */
    EG_T_HDR6   = 0x06, /* 1.0, 0, 0 */
    EG_T_HDR7   = 0x07, /* x=1.0 y=ymin z=int tool slot */
    EG_T_TOOL96 = 0x96, /* shank radius, tip radius, half angle */
    EG_T_TOOL97 = 0x97, /* tip transition radius, 0, int colour (FUN_004dca0c) */
    EG_T_NAME98 = 0x98, /* tool name chars 1..10 (Pascal string bytes) */
    EG_T_NAME99 = 0x99, /* tool name chars 11..20 */
    EG_T_TOOL9A = 0x9a,
    EG_T_TOOL9B = 0x9b, /* 10.0, depth-cut-done flag (1.0), plunge feed */
    EG_T_TOOL9C = 0x9c, /* feed, depth D, fly height */
    EG_T_TOOL9D = 0x9d  /* spindle, T+0x4e, (float)tool-change status */
};

/* ---- error codes (values of the binary's global error *PTR_DAT_0052f230 where it has one) ---- */
enum {
    EG_OK                = 0,
    EG_ERR_TOOL_RT       = 0x33,  /* 51: a tool row has tip radius <= 0.001 (FUN_00528784) */
    EG_ERR_OVERLAP       = 0x35,  /* 53: contours overlap / touch (FUN_0051203c) */
    EG_ERR_NODATA        = 100,   /* contour preparation left < 4 records (FUN_00528784) */
    EG_ERR_TC_LAST_A     = 0x389, /* -5: last tool is 'A' (3D only) while roughing != 'K' */
    EG_ERR_TC_CORNER     = 0x38a, /* -4: corner tool 'E' rules */
    EG_ERR_TC_GRUND3D    = 0x38b, /* -3: KonturenAuf 'G' with a '3'/'A' tool */
    EG_ERR_TC_RORDER     = 0x38c, /* -2: effective radii not increasing */
    EG_ERR_TC_NOTOOL     = 0x38d, /* -1: no first tool */
    EG_ERR_TOO_MANY      = 0x44d, /* contour > 99999/100000 points */
    EG_ERR_NOT_DECODED   = -1000, /* stage not decoded - see NOTES.md */
    EG_ERR_HANG          = -1001, /* the binary loops forever on this input */
    EG_ERR_MEM           = -1002,
    EG_ERR_IO            = -1003,
    EG_ERR_HEADER        = -1004  /* depth cut: slot file has no 0x9b header record (msg 0x3a) */
};

/* ---- record list helpers ---- */
void eg_recs_free(EgRecs *a);
int  eg_recs_push(EgRecs *a, int tag, double x, double y, double z);
int  eg_recs_push_rec(EgRecs *a, const EgRec *r);

/* ---- LG1 file format: 13-byte records, tag u8 + 3 x little-endian 32-bit fields.
 * Field typing (from the writers FUN_005045cc / FUN_0050cddc): tag 0x01 z, tag 0x07 z and
 * tag 0x97 z are int32 (stored in EgRec as the integer value); tags 0x98/0x99 carry raw
 * string bytes: x,y,z hold the three 32-bit words as unsigned integer values. All other
 * fields are f32. */
int eg_lg1_decode(const unsigned char *buf, size_t len, EgRecs *out);
int eg_lg1_encode(const EgRecs *in, unsigned char **buf, size_t *len);
int eg_lg1_read_file(const char *path, EgRecs *out);
int eg_lg1_write_file(const char *path, const EgRecs *in);

/* ---- tool library entry (block 0xf, 0x8c bytes per tool) ---- */
typedef struct {
    char  name[41];       /* +0x01 string[0x28] */
    float D;              /* +0x2a LEintauchT (depth) */
    float fly;            /* +0x2e LFlugHoehe */
    float rt;             /* +0x32 LFRadius (tip radius) */
    float angle;          /* +0x36 LFWinkel (half angle, deg) */
    float r3a;            /* +0x3a radius of the rounded tip transition */
    float Rs;             /* +0x3e LFSchaft (shank radius) */
    float feed;           /* +0x42 LVorschub */
    float plunge;         /* +0x46 LEintVorschZ */
    float spindle;        /* +0x4a LSpindelDreh */
    float f4e;            /* +0x4e (unnamed) */
    short wechsel;        /* +0x52 LWechselStat */
    short f54;            /* +0x54 (colour index; FUN_004dca0c maps it to RGB) */
    unsigned char capped; /* +0x8b set by FUN_004a2054 when the radius was capped at Rs */
} EgTool;

/* FUN_004a2054: effective (offset) radius of a tool at its depth D. Mutates t->D (<0 -> 0)
 * and t->capped like the binary. */
double eg_tool_eff_radius(EgTool *t);

/* ---- tool rows R (10 x 0x1d bytes) built by FUN_00529b50 ---- */
typedef struct {
    short lib;     /* +0x00 library index, -1 = none */
    short color;   /* +0x02 T+0x54 */
    float D;       /* +0x04 */
    float fly;     /* +0x08 */
    float rt;      /* +0x0c */
    float angle;   /* +0x10 */
    float r3a;     /* +0x14 */
    float R;       /* +0x18 offset radius */
    char  corr;    /* +0x1c KorrekturVerf ('2','3','A','E'; '3'... 0x33 for empty rows) */
} EgToolRow;

/* FUN_00529b50. lib/nlib = tool library, slot_idx/slot_corr = block 0x11 +0x10 (10 x {i16,char}).
 * Returns the code the binary stores (0x38e + dx; dx >= 0 = ok, dx = number of the last
 * checked slot) and sets *ok (the function's boolean result). */
int eg_tool_rows(EgTool *lib, int nlib, const short slot_idx[10], const char slot_corr[10],
                 char KonturenAuf, char AbraeumVerf, EgToolRow rows[10], int *ok);

/* Bounding box record pair as used by the binary: {tag, xmin, ymin, tag, xmax, ymax}. */
typedef struct { double xmin, ymin, xmax, ymax; } EgBox;

/* FUN_00528784 (loop part): last used slot, max(R + extra), the 0x33 check and the 0x451
 * warning. extra = *(float*)[52919c ebp+0x1c] which FUN_00529e38 never initialises (see
 * NOTES: pass 0 unless measured otherwise). */
typedef struct { int last; double maxR; int maxR_color; int warn451; int err; } EgSlotInfo;
EgSlotInfo eg_slot_info(const EgToolRow rows[10], double extra);

/* FUN_00504268: pre-computation width W. box = global extent PTR_DAT_0052ed80. */
double eg_width_W(const EgBox *box, double maxR);

/* FUN_004dd3e4: ENGCONT.LG1 records -> working layer (VekARH0.TMP). tol = 0.001 (pushed by
 * FUN_00529e94 @0x529eea). Returns EG_ERR_NOT_DECODED for arc/spline/text tags. */
int eg_load_layer(const EgRecs *in, EgRecs *out, double tol);

/* FUN_004fb97c (via FUN_00504154): contour preparation / point reduction ("VekARH0 -> VekAR1").
 * KParamNormGr = block 7 +0x15, W from eg_width_W; tolerance 1e-4 and the unused 0.01 are the
 * constants pushed by FUN_00528784. */
int eg_contour_prep(const EgRecs *in, EgRecs *out, int KParamNormGr, double W);

/* FUN_004f194c(VekAR1 -> VekAR10, mark=1, draw=1, 0): orientation normalisation and the
 * -4/-5 marker in the z of each contour's start record. */
int eg_orient(const EgRecs *in, EgRecs *out, int mark);

/* FUN_0051203c(VekAR10, 0, 0): 1 = overlap found (-> error 53 when KDatencheck). markers
 * (optional, may be NULL) receive up to 500 marker points as x,y pairs (block 0x25). */
int eg_overlap_check(const EgRecs *in, double *markers_xy, int *n_markers);

/* reverse condition of FUN_00528784 @0x5289a3 */
int eg_reverse_needed(int KGegenlauf, char GravurTyp);
/* FUN_004f129c -> FUN_004f10e8 -> FUN_004f0fe0: reverse every contour in place (new list). */
int eg_reverse(const EgRecs *in, EgRecs *out);

/* FUN_00519380 (KStartpunkt > 1, called from FUN_00528dac with flag = KGegenlauf ^ 1).
 * mode 2 = sharpest corner turning against the flag, mode 3 = middle of the longest
 * segment. Other modes: file left unchanged. */
int eg_startpoint(const EgRecs *in, EgRecs *out, int flag, int mode);

/* FUN_005045cc(slot, T, out, 10.0): the 12 tool header records of a slot file (+ the 13th,
 * tag 9, which FUN_00517b90 does not write). color = FUN_004dca0c(T->f54) (NOT DECODED:
 * caller supplies). pad = the 11-char string of resource 0xb (FUN_00497c44(0xb), observed as
 * spaces in the sample). */
int eg_tool_header(int slot, const EgTool *t, int color, const char *pad11, EgRecs *out);

/* FUN_005183e8 for one slot file (header + moves): removal of dangling 3-record "spikes".
 * Keeps the binary's index base (see NOTES). markers as in eg_overlap_check. */
int eg_spike_filter(EgRecs *slot, double *markers_xy, int *n_markers);

/* FUN_0050cc2c + FUN_0050cddc: append one slot file to the layer-1 list with the patched
 * header (box, count, end tag 9). */
int eg_block_append(EgRecs *layer, const EgRecs *slot);

/* FUN_0050d074 order: fills order[] with slot numbers in output order; returns the count. */
int eg_block_order(int KAbraeumZuerst, int order[16]);

/* FUN_00499380(layer1, out, 5, ...): final copy -> ENGTOOLP.LG1 (drops tag 0 records). */
int eg_final_copy(const EgRecs *layer, EgRecs *out);

/* FUN_004fa8b8 -> FUN_004fa5bc: depth cuts of one slot file (header + moves).
 * layers = SpantSchichtZ, aufteil = SpantAufteil ('L'/'V'), ramp_deg = SpantEintWi,
 * spiegelt = SpantSpiegelt, t = the slot's tool (from the header, FUN_004f9f48).
 * Ramps (FUN_004f9824) are NOT DECODED: returns EG_ERR_NOT_DECODED when one is needed. */
int eg_depth_cut(const EgRecs *slot, int layers, char aufteil, double ramp_deg, double spiegelt,
                 const EgTool *t, EgRecs *out);

/* ---- contour index (FUN_00510c6c): one entry per 0x0b-started group with > 1 record ---- */
typedef struct {
    int start, end;              /* +0x04 / +0x08 record indices (0x0b .. last before next 0x0b) */
    float sx, sy;                /* +0x10 / +0x14 start (the 0x0b) */
    float ex, ey;                /* +0x18 / +0x1c last record that is not 0x0b/0x80 */
    float xmin, ymin, xmax, ymax;/* +0x20..+0x2c box of 0x0a/0x0b/0x0c records */
    float ymid;                  /* +0x30 */
    float thr;                   /* +0x34 link threshold (param, or 0.7*z of a 0x80 record) */
    int type;                    /* +0x38: -5 (0x0b z = -5), -4 (z = -4), else 0 */
} EgContourIdx;
/* returns number of entries (>= 0) or error; box = union of the entry boxes (+0x3b) */
int eg_contour_index(const EgRecs *in, float thr, EgContourIdx **idx, EgBox *box);

/* FUN_00527850 + FUN_005274e8 + FUN_00527390 + FUN_00527280 + FUN_005277f0, then the
 * per-block iterator FUN_00527c5c: KBlockweise >= 2 block split of VekAR1.
 * mode = KBlockweise - 2 (0 rows top->bottom/cells left->right, 1 columns left->right/cells
 * top->bottom, 2 one cell); last_arg = 1 for GravurTyp 'V', -4 otherwise.
 * blocks[i] = records of block i (islands first, then the seed contour); types[i] = 'V' for
 * blocks ended by a 0x80 separator, 'E' for the trailing leftover block (FUN_0052919c sets
 * GravurTyp to it when GravurTyp != 'V'). Caller frees blocks[i], *blocks, *types. */
int eg_blocks(const EgRecs *vekar1, int mode, int last_arg, EgRecs **blocks, char **types,
              int *nblocks);

/* FUN_00528688 -> FUN_0051bff0 (Knachsortieren && !KAbrWegZusammen, AbraeumVerf C/D/Z):
 * re-sort of the roughing passes in slot 10. slot10 = slot-10 file (header + passes),
 * finish = records of the slot file of the last tool (collision test), stepover =
 * (1 - KAbraeumueberlapp(+0xc)/100) * rt[last]. variant (optional) receives the chosen
 * ordering (0 original, 1 grid, 2 by start y, 3 by start x, 4 distance from box corner,
 * 5 nearest-neighbour chain). */
int eg_rough_resort(const EgRecs *slot10, const EgRecs *finish, double stepover, EgRecs *out,
                    int *variant);


/* eg_pipe.c - port of the EStamp7.exe engrave pipeline stages around the geometry kernels.
 * See eg_pipe.h and NOTES.md (citations: function @address, dump line in
 * decomp/engrave/est/p_4b0000.c unless p_460000.c is named).
 *
 * Rules: structure, constants and order follow the binary. Values the binary keeps in f32
 * memory slots are rounded through (float) at the same places. Nothing here is a substitute
 * algorithm; undecoded parts return EG_ERR_NOT_DECODED.
 */


#define DEG2RAD 0.017453292519943295 /* TBYTE constant used throughout (e.g. 0x4a223c) */

static float F(double v) { return (float)v; }

/* ------------------------------------------------------------------ record lists */
void eg_recs_free(EgRecs *a)
{
    free(a->r);
    a->r = NULL;
    a->n = a->cap = 0;
}

static int eg_recs_reserve(EgRecs *a, int need)
{
    if (need <= a->cap) return EG_OK;
    int c = a->cap ? a->cap : 64;
    while (c < need) c *= 2;
    EgRec *p = (EgRec *)realloc(a->r, (size_t)c * sizeof(EgRec));
    if (!p) return EG_ERR_MEM;
    a->r = p;
    a->cap = c;
    return EG_OK;
}

int eg_recs_push(EgRecs *a, int tag, double x, double y, double z)
{
    if (eg_recs_reserve(a, a->n + 1)) return EG_ERR_MEM;
    a->r[a->n].tag = tag;
    a->r[a->n].x = x;
    a->r[a->n].y = y;
    a->r[a->n].z = z;
    a->n++;
    return EG_OK;
}

int eg_recs_push_rec(EgRecs *a, const EgRec *r) { return eg_recs_push(a, r->tag, r->x, r->y, r->z); }

/* ------------------------------------------------------------------ LG1 encode/decode */
static int field_is_int(int tag, int k)
{
    /* FUN_005045cc: rec0 (tag 1) +9 = int, rec3 (tag 7) +0x30 = int slot, rec5 (0x97) +0x4a =
       FUN_004dca0c colour; FUN_0050cddc writes tag-1 z as int count (line 65900). */
    if (k == 2 && (tag == EG_T_HDR1 || tag == EG_T_HDR7 || tag == EG_T_TOOL97)) return 1;
    if (tag == EG_T_NAME98 || tag == EG_T_NAME99) return 2; /* raw bytes */
    return 0;
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

int eg_lg1_decode(const unsigned char *buf, size_t len, EgRecs *out)
{
    for (size_t i = 0; i + 13 <= len; i += 13) {
        int tag = buf[i];
        double v[3];
        for (int k = 0; k < 3; k++) {
            uint32_t u = rd32(buf + i + 1 + 4 * k);
            int t = field_is_int(tag, k);
            if (t == 1) v[k] = (double)(int32_t)u;
            else if (t == 2) v[k] = (double)u;
            else {
                float f;
                memcpy(&f, &u, 4);
                v[k] = f;
            }
        }
        if (eg_recs_push(out, tag, v[0], v[1], v[2])) return EG_ERR_MEM;
    }
    return EG_OK;
}

int eg_lg1_encode(const EgRecs *in, unsigned char **buf, size_t *len)
{
    unsigned char *b = (unsigned char *)malloc((size_t)in->n * 13 + 1);
    if (!b) return EG_ERR_MEM;
    for (int i = 0; i < in->n; i++) {
        const EgRec *r = &in->r[i];
        unsigned char *p = b + (size_t)i * 13;
        double v[3] = { r->x, r->y, r->z };
        p[0] = (unsigned char)r->tag;
        for (int k = 0; k < 3; k++) {
            int t = field_is_int(r->tag, k);
            uint32_t u;
            if (t == 1) u = (uint32_t)(int32_t)v[k];
            else if (t == 2) u = (uint32_t)v[k];
            else {
                float f = (float)v[k];
                memcpy(&u, &f, 4);
            }
            wr32(p + 1 + 4 * k, u);
        }
    }
    *buf = b;
    *len = (size_t)in->n * 13;
    return EG_OK;
}

int eg_lg1_read_file(const char *path, EgRecs *out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return EG_ERR_IO;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return EG_ERR_IO; }
    unsigned char *b = (unsigned char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return EG_ERR_MEM; }
    size_t got = fread(b, 1, (size_t)n, f);
    fclose(f);
    int rc = eg_lg1_decode(b, got, out);
    free(b);
    return rc;
}

int eg_lg1_write_file(const char *path, const EgRecs *in)
{
    unsigned char *b;
    size_t n;
    int rc = eg_lg1_encode(in, &b, &n);
    if (rc) return rc;
    FILE *f = fopen(path, "wb");
    if (!f) { free(b); return EG_ERR_IO; }
    size_t w = fwrite(b, 1, n, f);
    fclose(f);
    free(b);
    return w == n ? EG_OK : EG_ERR_IO;
}

/* ------------------------------------------------------------------ small helpers */
/* FUN_004a2450: 2D distance of two records (f32 result). */
static float dist2d(const EgRec *a, const EgRec *b)
{
    double dx = F(b->x) - (double)F(a->x), dy = F(b->y) - (double)F(a->y);
    return F(sqrt(dx * dx + dy * dy));
}

/* FUN_004a1f04: tan of an f32 argument (0 for 0), f32 result. */
static float tan_f(float a)
{
    if (a == 0.0f) return 0.0f;
    return F(sin((double)a) / cos((double)a));
}

/* ------------------------------------------------------------------ FUN_004a2054 */
double eg_tool_eff_radius(EgTool *t)
{
    t->capped = 0;                                /* 0x4a205a */
    if (t->D < 0.0f) t->D = 0.0f;                 /* 0x4a2061..0x4a2072 */
    float r = t->rt;                              /* [esp+4] */
    float hD;                                     /* [esp+8] */
    if (t->r3a > 0.0f) {                          /* 0x4a207c */
        float s = F(((double)t->Rs - t->rt) / t->r3a);      /* [esp+0xc] */
        float g = F((90.0 - t->angle) * DEG2RAD);           /* [esp+0x10] */
        float sa;
        if (s >= 1.0f) sa = g;                               /* 0x4a20bc..0x4a20cc */
        else {
            if (s < 0.0f) s = 0.0f;                          /* 0x4a20d2 */
            sa = F(atan((double)s / sqrt(1.0 - (double)s * s))); /* 0x4a20e8..0x4a2115 */
            if (g < sa) sa = g;                              /* 0x4a2119 */
        }
        float h = F((1.0 - cos((double)sa)) * t->r3a);      /* [esp+0x14] */
        if (h > t->D) {                                      /* 0x4a214d */
            double a2 = (double)t->r3a * t->r3a;
            double b = (double)t->r3a - t->D;
            r = F(sqrt(a2 - b * b) + r);                     /* 0x4a215b..0x4a2183 */
            if (r > t->Rs) r = t->Rs;                        /* 0x4a2187 */
            return r;                                        /* 0x4a219c -> 0x4a222c */
        }
        r = F(sin((double)sa) * t->r3a + r);                 /* 0x4a21a8 */
        hD = h;
    } else {
        hD = 0.0f;                                           /* 0x4a21ce */
    }
    if (t->angle > 0.0f) {                                   /* 0x4a21d4 */
        float ta = tan_f(F(DEG2RAD * t->angle));
        r = F((double)ta * ((double)t->D - hD) + r);         /* 0x4a21f8..0x4a2205 */
        if (r > t->Rs) { r = t->Rs; t->capped = 1; }         /* 0x4a2209..0x4a221e */
    }
    return r;
}

/* ------------------------------------------------------------------ FUN_00529b50 */
/* FUN_00529af4: slot is usable if idx >= 0, D > 0 and (r3a > 0 or rt > 0) */
static int slot_ok(const EgTool *lib, int nlib, short idx)
{
    if (idx < 0 || idx >= nlib) return 0;
    const EgTool *t = &lib[idx];
    if (t->D <= 0.0f) return 0;
    if (t->r3a <= 0.0f && t->rt <= 0.0f) return 0;
    return 1;
}

int eg_tool_rows(EgTool *lib, int nlib, const short slot_idx[10], const char slot_corr[10],
                 char KonturenAuf, char AbraeumVerf, EgToolRow rows[10], int *ok)
{
    for (int s = 0; s < 10; s++) {                            /* loop 87800..87854 */
        EgToolRow *w = &rows[s];
        memset(w, 0, sizeof *w);
        if (!slot_ok(lib, nlib, slot_idx[s])) {
            w->lib = -1;
            w->corr = 0x33;                                   /* 87820 */
            continue;
        }
        EgTool *t = &lib[slot_idx[s]];
        w->lib = slot_idx[s];
        w->D = t->D;
        w->fly = t->fly;
        w->rt = t->rt;
        w->angle = t->angle;
        w->r3a = t->r3a;
        w->color = t->f54;
        w->corr = slot_corr[s];
        if (KonturenAuf == 'G') w->R = w->rt;                 /* 87834 */
        else w->R = F(eg_tool_eff_radius(t));                 /* 87838 */
    }
    int dx;                                                   /* 529c50.. */
    if (rows[0].lib < 0) dx = -1;
    else if (rows[1].lib < 0) dx = 1;
    else {
        int di = 1;
        while (di < 9 && !(rows[di - 1].R > rows[di].R) && rows[di + 1].lib >= 0) di++;
        dx = (rows[di - 1].R < rows[di].R) ? di : -2;         /* 0x529cab..0x529cd3 */
    }
    if (dx >= 0) {                                            /* 0x529ce7.. */
        int di = 1;
        for (;;) {
            if (di >= 9 || rows[di].lib < 0) break;
            if (rows[di - 1].corr == 'E' && F(rows[di - 1].rt * 2.5) > rows[di].rt) { dx = -4; break; }
            di++;
        }
        if (rows[di - 1].corr == 'E') dx = -4;                /* 0x529d41 */
        if (dx >= 0 && rows[di - 1].corr == 'A' && AbraeumVerf != 'K') dx = -5; /* 0x529d5d */
    }
    if (dx >= 0 && KonturenAuf == 'G') {                      /* 0x529d7f */
        for (int s = 0; s < 10; s++) {
            if (slot_idx[s] < 0) break;
            if (slot_corr[s] == '3' || slot_corr[s] == 'A') { dx = -3; break; }
        }
    }
    *ok = dx >= 0;
    return dx + 0x38e;                                        /* 0x529dae */
}

/* ------------------------------------------------------------------ FUN_00528784 loop */
EgSlotInfo eg_slot_info(const EgToolRow rows[10], double extra)
{
    EgSlotInfo s;
    memset(&s, 0, sizeof s);
    int last = 9;                                             /* 0x5287c6 */
    while (last != 0 && rows[last].lib < 0) last--;
    s.last = last;
    float maxR = 0.0f;                                        /* [parent-0x78] */
    for (int i = 0; i <= last; i++) {                         /* 0x528801.. */
        float v = F((double)rows[i].R + extra);
        if (v > maxR) { maxR = v; s.maxR_color = rows[i].color; }
        if (rows[i].r3a > 0.0f) s.warn451 = 1;                /* 0x528833 -> msg 0x451 */
        if (0.001 >= rows[i].rt) s.err = EG_ERR_TOOL_RT;      /* 0x528844 */
    }
    s.maxR = maxR;
    return s;
}

/* ------------------------------------------------------------------ FUN_00504268 */
double eg_width_W(const EgBox *box, double maxR)
{
    float W = F(1.05 * maxR);                                 /* 0x50427b */
    if (0.03 > W) W = 0.03f;                                  /* 0x504287 */
    float t = F(((double)F(box->xmax) - F(box->xmin)) * 0.0005); /* 0x50429e */
    if (W < t) W = t;
    t = F(((double)F(box->ymax) - F(box->ymin)) * 0.0005);    /* 0x5042c2 */
    if (W < t) W = t;
    if (0.01 + maxR > W) W = F(0.01 + maxR);                  /* 0x5042e6 */
    return W;
}

/* ------------------------------------------------------------------ FUN_004dd3e4 */
typedef struct {
    EgRec last;    /* [ebp-0x1c] last emitted record */
    EgRec pend;    /* [ebp-0x3c] pending move-to */
    int has_pend;  /* [ebp-0x3d] */
    float tol;     /* [ebp+0xc] */
    EgRecs *out;
} LoadSt;

/* FUN_004dd0d4 */
static int load_emit(LoadSt *st, const EgRec *r)
{
    if (r->tag == st->last.tag && dist2d(r, &st->last) <= st->tol && F(r->z) == F(st->last.z))
        return EG_OK;                                         /* 0x4dd0dc..0x4dd10d */
    if (eg_recs_push_rec(st->out, r)) return EG_ERR_MEM;      /* FUN_004dd094 */
    st->last = *r;
    return EG_OK;
}

/* FUN_004dd168(z of the current record) */
static int load_flush_pend(LoadSt *st, double z)
{
    if (!st->has_pend) return EG_OK;
    int same = 0.001 > dist2d(&st->pend, &st->last);          /* FUN_004a2490 */
    if (!same || F(st->pend.z) != F(z)) {
        st->has_pend = 0;
        return load_emit(st, &st->pend);
    }
    return EG_OK;
}

int eg_load_layer(const EgRecs *in, EgRecs *out, double tol)
{
    LoadSt st;
    memset(&st, 0, sizeof st);
    st.out = out;
    st.tol = F(tol);
    /* initial values 0x4dd3e4 lines 30723..30748: tag 0, x = y = -99999, z = 0; pending flag 1 */
    st.last.tag = 0; st.last.x = st.last.y = -99999.0; st.last.z = 0.0;
    st.pend = st.last;
    st.has_pend = 1;
    for (int i = 0; i < in->n; i++) {
        const EgRec *r = &in->r[i];
        int t = r->tag;
        int rc = EG_OK;
        switch (t) {
        case 0x01: case 0x05: case 0x06: case 0x07: case 0x09: case 0x16:
            rc = eg_recs_push_rec(out, r);                    /* FUN_004dd094: raw append */
            break;
        case EG_T_LINE: case EG_T_POINT:                      /* 30856 */
            rc = load_flush_pend(&st, r->z);
            if (!rc) rc = load_emit(&st, r);
            break;
        case EG_T_MOVE:                                       /* 30866: keep as pending only */
            st.pend = *r;
            st.has_pend = 1;
            break;
        case 0x1b:
            rc = load_emit(&st, r);                           /* 31079 */
            break;
        case 0x0e: case 0x0f: case 0x14:                      /* state only, no output */
            break;
        case 0x0d: case 0x10: case 0x11: case 0x12: case 0x15: case 0x19: case 0x1a: case 0x80:
            return EG_ERR_NOT_DECODED;                        /* arcs/splines/text: NOT DECODED */
        default:
            if (t >= 0x96 && t <= 0x9d) rc = eg_recs_push_rec(out, r);
            break;
        }
        if (rc) return rc;
    }
    return EG_OK;
}

/* ------------------------------------------------------------------ FUN_004fb97c / FUN_004fab4c */
/* FUN_004fab4c: two-pass point reduction of one contour p[0..n] (n = index of last point).
 * Coordinates are already divided by scale. Returns the new count in *out_n (p rewritten). */
static int reduce_contour(EgRec *p, int n, float scale, float tol, int *out_n)
{
    if (n > 32767) return EG_ERR_NOT_DECODED; /* 16-bit loop counters (0x4faba7) */
    EgRec *w = (EgRec *)malloc((size_t)(n + 2) * sizeof(EgRec));
    if (!w) return EG_ERR_MEM;
    float eps = F(0.005 / scale);                             /* 0x4fab62 [ebp-0x20] */
    float tol1 = F(tol * 1.0);                                /* 0x4fab74 [ebp-0x10] */
    /* pass 1 (0x4fab81..0x4facd5) */
    w[0] = p[0];
    int m = 1;
    EgRec A = p[0];
    int last = 0;
    for (int i = 1; i <= n - 1; i++) {
        double c = fabs((A.x - p[i].x) * (A.y - p[i + 1].y) - (A.x - p[i + 1].x) * (A.y - p[i].y));
        if (c > tol1) {
            w[m++] = p[i];
            A = p[i];
            last = i;
        } else if (i - last > 2) {
            int mid = i - ((i - last) >> 1);                  /* 0x4fac2c..0x4fac41 */
            double c2 = fabs((A.x - p[mid].x) * (A.y - p[i + 1].y) - (A.x - p[i + 1].x) * (A.y - p[mid].y));
            if (c2 > tol1) {
                w[m++] = p[mid];
                A = p[mid];
                last = mid;
            }
        }
    }
    w[m] = p[n];                                              /* 0x4facdb (m not incremented) */
    /* pass 2 (0x4facf6..0x4fae3b) */
    A = w[0];
    p[0] = w[0];
    int out = 1;
    EgRec B = w[1], C = w[1];
    float dx = F(B.x - A.x), dy = F(B.y - A.y);
    for (int j = 2; j <= m; j++) {
        EgRec Q = w[j];
        if (fabs(Q.x - B.x) > eps || fabs(Q.y - B.y) > eps) {
            float d;
            if (fabs((double)dx) > fabs((double)dy))
                d = F(fabs(Q.y - ((Q.x - A.x) * dy / dx + A.y)));
            else
                d = F(fabs(Q.x - ((Q.y - A.y) * dx / dy + A.x)));
            if (d > tol) {
                p[out++] = C;
                A = B;
                B = Q;
                dx = F(B.x - A.x);
                dy = F(B.y - A.y);
            }
            C = Q;
        }
    }
    /* closing (0x4fae41..0x4faedc) */
    EgRec *L = &p[out - 1];
    if (F(p[0].x) != F(L->x) || F(p[0].y) != F(L->y)) {
        if (fabs(p[0].x - L->x) > eps || fabs(p[0].y - L->y) > eps) {
            p[out] = p[0];
            p[out].tag = EG_T_LINE;
            out++;
        } else {
            *L = p[0];
            L->tag = EG_T_LINE;
        }
    }
    free(w);
    if (out > 0x7ffe) return EG_ERR_NOT_DECODED; /* retry with larger tolerance (0x4faeec) reads
                                                    one record past the list - NOT DECODED */
    *out_n = out;
    return EG_OK;
}

/* FUN_004fb724 -> FUN_004fb624 for one contour buf[0..cnt] */
static int prep_flush(EgRec *buf, int cnt, float scale, float tol, EgRecs *out)
{
    if (cnt > 0xbffc) return EG_ERR_NOT_DECODED;              /* split path FUN_004faf4c/FUN_004fb334 */
    int nout = 0;
    int rc = reduce_contour(buf, cnt, scale, tol, &nout);
    if (rc) return rc;
    if (nout > 3) {                                           /* 0x4fb65c */
        for (int k = 0; k < nout; k++) {
            EgRec q = buf[k];
            q.x = F(F(q.x) * scale);                          /* 0x4fb68a.. */
            q.y = F(F(q.y) * scale);
            if (eg_recs_push_rec(out, &q)) return EG_ERR_MEM; /* FUN_004bbdcc */
        }
    }
    return EG_OK;
}

int eg_contour_prep(const EgRecs *in, EgRecs *out, int KParamNormGr, double W)
{
    float pnorm = (float)(-(int)(unsigned char)KParamNormGr); /* 0x5288d1..0x5288e6 */
    float tol = 1e-4f;                                        /* pushed 0x38d1b717 */
    if (pnorm == -6.0f) return EG_ERR_NOT_DECODED;            /* FUN_004c3cf0 path */
    float scale;
    if (pnorm == -5.0f) scale = 1.0f;                         /* 0x4fba69 */
    else {
        /* FUN_004fb7b8: box of tags 0x0a..0x0c, box init FUN_004c4434 (+-100000) */
        float x0 = 100000.0f, y0 = 100000.0f, x1 = -100000.0f, y1 = -100000.0f;
        for (int i = 0; i < in->n; i++) {
            const EgRec *r = &in->r[i];
            if (r->tag < 10 || r->tag > 12) continue;
            if (F(r->x) < x0) x0 = F(r->x);
            if (F(r->y) < y0) y0 = F(r->y);
            if (x1 < F(r->x)) x1 = F(r->x);
            if (y1 < F(r->y)) y1 = F(r->y);
        }
        float size = F((double)x1 - x0);
        if (size < F((double)y1 - y0)) size = F((double)y1 - y0);
        int k = (int)nearbyint(pnorm);                        /* FUN_00403390 Round */
        if (k == -4) scale = F(size / 500.0);
        else if (k == -3) scale = F(size / 200.0);
        else if (k == -2) scale = F(size / 100.0);
        else if (k == -1) scale = F(size / 50.0);
        else scale = 1.0f;
        if (W > 1.0) scale = F((double)scale * F(W) / 1.0);   /* 0x4fb932 */
    }
    if (0.0001 > tol) tol = 1e-4f;                            /* 0x4fbb24 */
    if (in->n <= 2) return EG_OK;                             /* 0x4fbaf4 */

    EgRec *buf = (EgRec *)malloc((size_t)(in->n + 2) * sizeof(EgRec));
    if (!buf) return EG_ERR_MEM;
    int cnt = 0;            /* [ebp-0x154] index of the last stored point */
    int inside = 0;         /* [ebp-0x409] */
    int rc = EG_OK;
    buf[0].tag = 0; buf[0].x = buf[0].y = buf[0].z = 0.0;
    for (int i = 0; i < in->n && !rc; i++) {
        EgRec r = in->r[i];
        r.x = F(F(r.x) / scale);                              /* 0x4fbb9f..0x4fbbb9 */
        r.y = F(F(r.y) / scale);
        if (!inside) {
            if (r.tag == EG_T_LINE) {
                cnt++;
                buf[cnt] = r;
                inside = 1;
            } else if (r.tag == EG_T_MOVE) {
                buf[0] = r;
            }
        } else if (r.tag == EG_T_LINE) {
            if (cnt < 0xbffd) { cnt++; buf[cnt] = r; }        /* 0x4fbbda */
        } else if (r.tag == EG_T_MOVE) {
            if (cnt > 2) rc = prep_flush(buf, cnt, scale, tol, out); /* 0x4fbc16 */
            inside = 0;
            buf[0] = r;
            cnt = 0;
        }
    }
    if (!rc && inside && cnt > 2) rc = prep_flush(buf, cnt, scale, tol, out); /* 0x4fbcd? end */
    free(buf);
    return rc;
}

/* ------------------------------------------------------------------ FUN_004f194c */
/* FUN_004f12f0: leftmost crossing of the contour with the line y = *y; orientation flag =
 * (ya > yb) of that edge. Shifts *y by 2e-5 while it hits a vertex. */
static void probe_x(const EgRec *p, int n, int *orient, double *xprobe, double *y)
{
    if (n < 3) { *xprobe = 10000.0; return; }
    for (;;) {
        float ya = 0.0f, yb = 0.0f;                           /* [ebp-0x28],[ebp-0x2c] */
        int hit = 0;
        *xprobe = 10000.0;
        EgRec prev = p[0];
        for (int i = 0; i < n; i++) {
            const EgRec *c = &p[i];
            if ((double)F(prev.y) == *y || (double)F(c->y) == *y) { hit = 1; break; }
            double d = (double)F(c->y) - F(prev.y);
            if (d != 0.0) {
                double t = (*y - F(prev.y)) / d;
                if (t > 0.0 && t < 1.0) {
                    float x = F(((double)F(c->x) - F(prev.x)) * t + F(prev.x));
                    if (x < *xprobe) { *xprobe = x; ya = F(prev.y); yb = F(c->y); }
                }
            }
            prev = *c;
        }
        if (!hit) { *orient = ya > yb; return; }
        *y += 2e-05;                                          /* 0x4f1416 */
    }
}

/* FUN_004f1458 (param_7 = 0): parity of crossings left of the probe point over ALL records. */
static void parity_inside(const EgRecs *all, const EgRec *p, int n, int *inside, int *orient, double *y)
{
    for (;;) {
        double xp;
        probe_x(p, n, orient, &xp, y);
        int count = 0, hit = 0;
        EgRec prev = all->r[0];
        for (int i = 0; i < all->n; i++) {
            const EgRec *c = &all->r[i];
            if ((double)F(prev.y) == *y || (double)F(c->y) == *y) { hit = 1; break; }
            if (c->tag != EG_T_MOVE) {
                double d = (double)F(c->y) - F(prev.y);
                if (d != 0.0) {
                    double t = (*y - F(prev.y)) / d;
                    if (t > 0.0 && t < 1.0) {
                        double xc = ((double)F(c->x) - F(prev.x)) * t + F(prev.x);
                        if (xc - xp < 0.0005) count++;        /* 0x4f155f..0x4f157c */
                    }
                }
            }
            prev = *c;
        }
        if (!hit) { *inside = (count & 1) != 0; return; }
        *y += 2e-05;                                          /* 0x4f15a1 */
    }
}

int eg_orient(const EgRecs *in, EgRecs *out, int mark)
{
    if (in->n <= 2) return EG_OK;                             /* FUN_004c48c4 > 0x19 bytes, count > 2 */
    EgRec *pts = (EgRec *)malloc((size_t)(in->n + 2) * sizeof(EgRec));
    if (!pts) return EG_ERR_MEM;
    int np = 0, coll = 0, rc = EG_OK;
    EgRec saved = { 0, 0, 0, 0 };
    float ymin = 100000.0f, ymax = -100000.0f;
    for (int i = 0; i <= in->n && !rc; i++) {
        int flush = (i == in->n);
        const EgRec *r = flush ? NULL : &in->r[i];
        if (!flush && (r->tag == EG_T_LINE || r->tag == EG_T_POINT)) {
            if (!coll) {
                coll = 1;
                ymin = 100000.0f; ymax = -100000.0f;          /* FUN_004c4434 */
                pts[np++] = saved;
                if (F(saved.y) < ymin) ymin = F(saved.y);
                if (ymax < F(saved.y)) ymax = F(saved.y);
            }
            pts[np++] = *r;
            if (F(r->y) < ymin) ymin = F(r->y);
            if (ymax < F(r->y)) ymax = F(r->y);
            if (np > 99999) rc = EG_ERR_TOO_MANY;             /* FUN_004f1650 */
            continue;
        }
        if (!flush && r->tag != EG_T_MOVE) continue;
        if (flush || coll) {
            /* FUN_004f188c */
            if (np > 3) {
                double ym = (double)F(((double)ymax + ymin) / 2.0);
                int inside = 0, orient = 0;
                parity_inside(in, pts, np, &inside, &orient, &ym);
                int code;
                if (orient == inside) {                       /* FUN_004f169c: as is */
                    code = orient == 0 ? 9 : 10;
                    if (mark) pts[0].z = code == 10 ? -4.0 : -5.0;
                    for (int k = 0; k < np && !rc; k++) rc = eg_recs_push_rec(out, &pts[k]);
                } else {                                      /* FUN_004f176c: reversed */
                    code = orient == 0 ? 10 : 9;
                    EgRec f = pts[np - 1];
                    f.tag = EG_T_MOVE;
                    if (mark) f.z = code == 10 ? -4.0 : -5.0;
                    rc = eg_recs_push_rec(out, &f);
                    for (int k = np - 2; k >= 0 && !rc; k--) {
                        EgRec q = pts[k];
                        q.tag = EG_T_LINE;
                        rc = eg_recs_push_rec(out, &q);
                    }
                }
            }
            np = 0;
            coll = 0;
        }
        if (!flush) saved = *r;
    }
    free(pts);
    return rc;
}

/* ------------------------------------------------------------------ FUN_0051203c */
typedef struct { int tag; float x, y; } Seg9;   /* 9-byte cell records (tag, x, y) */

typedef struct {
    float x0, x1, y0, y1;   /* 5117e8 [ebp-4],[ebp-8],[ebp-0xc],[ebp-0x10] */
} Cell;

/* FUN_00511790 */
static int outcode(const Cell *c, float x, float y)
{
    int k = 0;
    if (c->x0 <= x) { if (c->x1 < x) k = 2; } else k = 1;
    if (c->y0 <= y) { if (c->y1 < y) k |= 8; } else k |= 4;
    return k;
}

typedef struct {
    int nmark, abort, ninter, nnear;
    double *mxy; int mcap;
} OvSt;

static void ov_mark(OvSt *st, double x, double y)
{
    if (st->mxy && st->nmark < st->mcap) { st->mxy[2 * st->nmark] = x; st->mxy[2 * st->nmark + 1] = y; }
    st->nmark++;
}

int eg_overlap_check(const EgRecs *in, double *markers_xy, int *n_markers)
{
    OvSt st;
    memset(&st, 0, sizeof st);
    st.mxy = markers_xy;
    st.mcap = markers_xy ? 500 : 0;
    if (in->n == 0) { if (n_markers) *n_markers = 0; return 1; } /* 0x5120bb -> bl = 1 */
    EgRec *a = (EgRec *)malloc((size_t)in->n * sizeof(EgRec));
    Seg9 *s = (Seg9 *)malloc((size_t)(2 * in->n + 2) * sizeof(Seg9));
    if (!a || !s) { free(a); free(s); return EG_ERR_MEM; }
    memcpy(a, in->r, (size_t)in->n * sizeof(EgRec));
    /* FUN_00511e58 */
    int k = (int)nearbyint(sqrt((double)in->n / (in->n < 10000 ? 80.0 : 100.0)));
    if (k < 1) k = 1;
    Cell cells_one = { -10000.0f, 10000.0f, -10000.0f, 10000.0f };
    float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0, cw = 0, ch = 0, yrow = 0, xcol = 0;
    int single = k < 2;
    if (!single) {
        /* FUN_004dce90 box over tags 0x0a,0x0b,0x0c,0x11,0x12 (init by FUN_00499018: NOT
           DECODED - assumed empty box, only matters for degenerate input) */
        bx0 = by0 = 3.0e38f; bx1 = by1 = -3.0e38f;
        for (int i = 0; i < in->n; i++) {
            int t = in->r[i].tag;
            if (!(t == 0x0a || t == 0x0b || t == 0x0c || t == 0x11 || t == 0x12)) continue;
            float x = F(in->r[i].x), y = F(in->r[i].y);
            if (x < bx0) bx0 = x;
            if (y < by0) by0 = y;
            if (bx1 < x) bx1 = x;
            if (by1 < y) by1 = y;
        }
        cw = F(((double)bx1 - bx0) / k);
        ch = F(((double)by1 - by0) / k);
        yrow = F((double)by1 - ch);
        xcol = bx0;
    }
    for (;;) {
        Cell c;
        if (single) c = cells_one;
        else { c.x0 = xcol; c.x1 = F((double)xcol + cw); c.y0 = yrow; c.y1 = F((double)yrow + ch); }
        /* FUN_005117e8 */
        int n = 0;
        EgRec prev; prev.tag = 0; prev.x = prev.y = prev.z = 0.0;
        for (int i = 0; i < in->n; i++) {
            EgRec *p = &a[i];
            if (p->tag == EG_T_LINE || p->tag == EG_T_POINT) {
                if (prev.tag != 0) {
                    if (0.001 < fabs(F(p->x) - (double)F(prev.x)) || 0.001 < fabs(F(p->y) - (double)F(prev.y))) {
                        int o1 = outcode(&c, F(prev.x), F(prev.y));
                        int o2 = outcode(&c, F(p->x), F(p->y));
                        if ((o1 & o2) == 0) {
                            if (o1 != 0 || prev.tag == EG_T_MOVE) {
                                s[n].tag = EG_T_MOVE; s[n].x = F(prev.x); s[n].y = F(prev.y); n++;
                            }
                            if (p->tag == EG_T_POINT) p->tag = EG_T_LINE;
                            s[n].tag = p->tag; s[n].x = F(p->x); s[n].y = F(p->y); n++;
                        }
                    } else {
                        p->tag = prev.tag;                    /* 0x51190e */
                    }
                }
                prev = *p;
            } else if (p->tag == EG_T_MOVE) {
                prev = *p;
            }
        }
        /* FUN_00511960, strict mode ([parent+0x18] = 1) */
        if (n > 3) {
            for (int i = 1; i <= n - 3 && !st.abort; i++) {
                if (s[i].tag == EG_T_MOVE) continue;
                float ax = F((double)s[i].x - s[i - 1].x), ay = F((double)s[i].y - s[i - 1].y);
                for (int j = i + 2; j <= n - 1; j++) {
                    if (s[j].tag == EG_T_MOVE) continue;
                    float bx = F((double)s[j - 1].x - s[j].x), by = F((double)s[j - 1].y - s[j].y);
                    float den = F((double)ay * bx - (double)ax * by);
                    if (den == 0.0f) continue;
                    float t = F((((double)s[j - 1].y - s[i - 1].y) * bx - ((double)s[j - 1].x - s[i - 1].x) * by) / den);
                    if (t <= 1e-05 || 0.999999 < t || t == 1.0f) continue;
                    float u = F((((double)s[j - 1].x - s[i - 1].x) * ay - ((double)s[j - 1].y - s[i - 1].y) * ax) / den);
                    if (u <= 1e-05 || 0.999999 < u || u == 1.0f) continue;
                    if (st.nmark > 19999) { st.abort = 1; break; }
                    ov_mark(&st, F((double)t * ax + s[i - 1].x), F((double)t * ay + s[i - 1].y));
                    st.ninter++;
                }
            }
        }
        /* FUN_00511cb4 */
        if (n > 1 && !st.abort) {
            for (int i = 0; i <= n - 2 && !st.abort; i++) {
                if (s[i].tag == EG_T_MOVE) continue;
                for (int j = i + 1; j <= n - 1; j++) {
                    if (s[j].tag == EG_T_MOVE) continue;
                    double dx = (double)s[j].x - s[i].x, dy = (double)s[j].y - s[i].y;
                    if (dx * dx + dy * dy < 1e-06) {
                        if (st.nmark > 19999) { st.abort = 1; break; }
                        ov_mark(&st, s[j].x, s[j].y);
                        st.nnear++;
                    }
                }
            }
        }
        if (single || st.abort) break;
        xcol = F((double)xcol + cw);
        if (xcol <= F((double)bx1 - cw * 0.5)) continue;
        yrow = F((double)yrow - ch);
        if (!(by0 <= F(ch * 0.5 + (double)yrow))) break;
        xcol = bx0;
    }
    free(a);
    free(s);
    int nm = st.nmark < 500 ? st.nmark : 500;                 /* 0x5120f8 */
    if (n_markers) *n_markers = nm;
    if (nm > 0) return 1;                                     /* 0x512324 */
    return st.abort ? 1 : 0;                                  /* 0x512328 */
}

/* ------------------------------------------------------------------ reverse */
int eg_reverse_needed(int KGegenlauf, char GravurTyp)
{
    /* 0x5289a3: (Gegenlauf ^ 1) != (GravurTyp == 'V') */
    return ((KGegenlauf & 0xff) ^ 1) != (GravurTyp == 'V');
}

/* FUN_004f0fe0 */
static int reverse_one(EgRecs *out, const EgRec *c, int n)
{
    if (n < 1) return EG_OK;
    double zb = 0.0;
    for (int i = 0; i < n; i++) if (c[i].tag == EG_T_MOVE) zb = c[i].z;
    for (int i = n - 1; i >= 0; i--) {
        EgRec q = c[i];
        q.tag = (i == n - 1) ? EG_T_MOVE : EG_T_LINE;
        if (i == n - 1) q.z = zb;
        if (eg_recs_push_rec(out, &q)) return EG_ERR_MEM;
    }
    return EG_OK;
}

/* FUN_004f10e8 */
int eg_reverse(const EgRecs *in, EgRecs *out)
{
    EgRec *b = (EgRec *)malloc((size_t)(in->n + 1) * sizeof(EgRec));
    if (!b) return EG_ERR_MEM;
    int n = 0, first = 1, rc = EG_OK, over = 0;
    for (int i = 0; i < in->n && !rc; i++) {
        const EgRec *r = &in->r[i];
        if (r->tag == EG_T_LINE || r->tag == EG_T_POINT) {
            b[n++] = *r;
            if (n > 100000) { n = 100000; over = 1; }
        } else if (r->tag == EG_T_MOVE) {
            if (!first) { rc = reverse_one(out, b, n); n = 0; }
            else first = 0;
            b[n++] = *r;
        }
    }
    if (!rc) rc = reverse_one(out, b, n);
    free(b);
    if (!rc && over) rc = EG_ERR_TOO_MANY;
    return rc;
}

/* ------------------------------------------------------------------ FUN_00519380 */
/* FUN_00519088: corner metric of B between A and C */
static float corner_metric(const EgRec *A, const EgRec *B, const EgRec *C, int flag)
{
    double ax = F(A->x), ay = F(A->y), bx = F(B->x), by = F(B->y), cx = F(C->x), cy = F(C->y);
    double cr = (bx - ax) * cy + ((ax * by - ay * bx) - (by - ay) * cx);
    if ((cr < 0.0) == (flag != 0)) return 4.0f;
    float u1 = F(bx - ax), u2 = F(by - ay), v1 = F(cx - bx), v2 = F(cy - by);
    float l1 = F(sqrt((double)u1 * u1 + (double)u2 * u2)), l2 = F(sqrt((double)v1 * v1 + (double)v2 * v2));
    if (l1 <= 0.0f || l2 <= 0.0f) return 0.0f;
    float sx = F((double)v1 / l2 + (double)u1 / l1), sy = F((double)v2 / l2 + (double)u2 / l1);
    return F((double)sx * sx + (double)sy * sy);
}

/* FUN_00518f7c: rotate contour c[0..n-1] to start at k */
static int rotate_out(EgRecs *out, EgRec *c, int n, int k)
{
    EgRec q = c[k];
    q.tag = EG_T_MOVE;
    if (eg_recs_push_rec(out, &q)) return EG_ERR_MEM;
    for (int i = k + 1; i <= n - 1; i++) if (eg_recs_push_rec(out, &c[i])) return EG_ERR_MEM;
    c[k].tag = EG_T_LINE;
    for (int i = 1; i <= k; i++) if (eg_recs_push_rec(out, &c[i])) return EG_ERR_MEM;
    return EG_OK;
}

static int start_one(EgRecs *out, EgRec *c, int n, int flag, int mode)
{
    if (mode == 2) {                                          /* FUN_005191dc */
        float best = 4.0f;
        int k = 0;
        for (int i = 0; i <= n - 2; i++) {
            const EgRec *A = (i == 0) ? &c[n - 2] : &c[i - 1];
            float m = corner_metric(A, &c[i], &c[i + 1], flag);
            if (m < best) { best = m; k = i; }
        }
        return rotate_out(out, c, n, k);
    }
    if (mode == 3) {                                          /* FUN_00519274 */
        float best = 0.0f;
        int k = 0;
        for (int i = 0; i <= n - 2; i++) {
            float d = dist2d(&c[i], &c[i + 1]);               /* FUN_004a23e0 */
            if (best < d) { k = i; best = d; }
        }
        if (k + 1 <= n - 1) memmove(&c[k + 2], &c[k + 1], (size_t)(n - k - 1) * sizeof(EgRec));
        c[k + 1].x = F(((double)F(c[k].x) + F(c[k + 1].x)) / 2.0);
        c[k + 1].y = F(((double)F(c[k].y) + F(c[k + 1].y)) / 2.0);
        n++;
        return rotate_out(out, c, n, k + 1);
    }
    return EG_OK;
}

int eg_startpoint(const EgRecs *in, EgRecs *out, int flag, int mode)
{
    if (mode == 1 || in->n == 0) {                            /* 0x519380: param_3 != 1 */
        for (int i = 0; i < in->n; i++) if (eg_recs_push_rec(out, &in->r[i])) return EG_ERR_MEM;
        return EG_OK;
    }
    EgRec *c = (EgRec *)malloc((size_t)(in->n + 2) * sizeof(EgRec));
    if (!c) return EG_ERR_MEM;
    EgRecs tmp = { NULL, 0, 0 };
    int n = 0, rc = EG_OK;
    for (int i = 0; i < in->n && !rc; i++) {
        if (in->r[i].tag == EG_T_MOVE) {
            if (n > 3) rc = start_one(&tmp, c, n, flag, mode);
            n = 0;
        }
        c[n++] = in->r[i];
    }
    if (!rc && n > 3) rc = start_one(&tmp, c, n, flag, mode);
    free(c);
    if (!rc) {
        if (tmp.n > 0) {                                      /* FUN_0049a41c only when count > 0 */
            for (int i = 0; i < tmp.n && !rc; i++) rc = eg_recs_push_rec(out, &tmp.r[i]);
        } else {
            for (int i = 0; i < in->n && !rc; i++) rc = eg_recs_push_rec(out, &in->r[i]);
        }
    }
    eg_recs_free(&tmp);
    return rc;
}

/* ------------------------------------------------------------------ FUN_005045cc */
static uint32_t le_word(const unsigned char *b) { return rd32(b); }

int eg_tool_header(int slot, const EgTool *t, int color, const char *pad11, EgRecs *out)
{
    int rc = 0;
    rc |= eg_recs_push(out, EG_T_HDR1, 0.0, 0.0, 1);
    rc |= eg_recs_push(out, EG_T_HDR5, 0.0, 0.0, 0.0);
    rc |= eg_recs_push(out, EG_T_HDR6, 1.0, 0.0, 0.0);
    rc |= eg_recs_push(out, EG_T_HDR7, 1.0, 0.0, slot & 0xffff);
    rc |= eg_recs_push(out, EG_T_TOOL96, t->Rs, t->rt, t->angle);
    rc |= eg_recs_push(out, EG_T_TOOL97, t->r3a, 0.0, color);
    /* 0x98 / 0x99: Copy(name + pad, 1, 11) and Copy(Copy(name, 11, 11) + pad, 1, 11) as short
       strings; byte 12 of the record (the 11th char) is then zeroed (lines 59636, 59651). */
    for (int part = 0; part < 2; part++) {
        char s[64];
        size_t nl = strlen(t->name);
        const char *src = t->name;
        size_t from = part ? 10 : 0;                          /* Copy(s, 11, 11) */
        char tmp[64] = { 0 };
        if (from < nl) {
            size_t k = nl - from;
            if (k > 11) k = 11;
            memcpy(tmp, src + from, k);
        }
        snprintf(s, sizeof s, "%s%s", tmp, pad11 ? pad11 : "");
        unsigned char b[12] = { 0 };
        size_t L = strlen(s);
        if (L > 11) L = 11;
        b[0] = (unsigned char)L;
        memcpy(b + 1, s, L);
        b[11] = 0;
        rc |= eg_recs_push(out, part ? EG_T_NAME99 : EG_T_NAME98, le_word(b), le_word(b + 4), le_word(b + 8));
    }
    rc |= eg_recs_push(out, EG_T_TOOL9A, 0.0, 0.0, 0.0);
    rc |= eg_recs_push(out, EG_T_TOOL9B, 10.0, 0.0, t->plunge);
    rc |= eg_recs_push(out, EG_T_TOOL9C, t->feed, t->D, t->fly);
    rc |= eg_recs_push(out, EG_T_TOOL9D, t->spindle, t->f4e, (float)(unsigned short)t->wechsel);
    return rc ? EG_ERR_MEM : EG_OK;
}

/* ------------------------------------------------------------------ FUN_005183e8 */
typedef struct { int idx; float x0, y0, x1, y1; int active; } Spike;

static int spike_connected(const EgRecs *f, const Spike *e)
{
    /* FUN_00518100 */
    EgRec prev; prev.tag = 0; prev.x = prev.y = prev.z = 0.0;
    for (int i = 12; i < f->n; i++) {
        const EgRec *r = &f->r[i];
        if (r->tag == EG_T_MOVE) {
            if (prev.tag == EG_T_LINE || prev.tag == EG_T_POINT) {
                double dx = (double)e->x0 - F(prev.x), dy = (double)e->y0 - F(prev.y);
                if (sqrt(dx * dx + dy * dy) < 0.001) return 1;
            }
            double dx = (double)e->x1 - F(r->x), dy = (double)e->y1 - F(r->y);
            if (sqrt(dx * dx + dy * dy) < 0.001) return 1;
        }
        prev = *r;
    }
    if (prev.tag == EG_T_LINE || prev.tag == EG_T_POINT) {
        double dx = (double)e->x0 - F(prev.x), dy = (double)e->y0 - F(prev.y);
        if (sqrt(dx * dx + dy * dy) < 0.001) return 1;
    }
    return 0;
}

int eg_spike_filter(EgRecs *f, double *markers_xy, int *n_markers)
{
    if (f->n <= 12) return EG_OK;                             /* FUN_00518574: size > 0xc */
    Spike *E = (Spike *)calloc((size_t)f->n + 1, sizeof(Spike));
    if (!E) return EG_ERR_MEM;
    int ne = 0;
    EgRec rec[4];
    memset(rec, 0, sizeof rec);
    int cnt = 4, recidx = 0, cur_idx = -1;
    for (int i = 12; i <= f->n; i++) {
        int end = (i == f->n);
        const EgRec *r = end ? NULL : &f->r[i];
        if (end || r->tag == EG_T_MOVE) {
            /* FUN_00518344 */
            if (cnt < 4 && rec[1].tag == EG_T_POINT && rec[2].tag == EG_T_LINE) {
                const EgRec *a = &rec[0], *b = &rec[cnt - 1];
                int found = -1;                               /* FUN_00517cdc */
                for (int k = 0; k < ne; k++) {
                    if (!E[k].active) continue;
                    double dx = (double)E[k].x0 - F(a->x), dy = (double)E[k].y0 - F(a->y);
                    if (!(sqrt(dx * dx + dy * dy) < 0.001)) continue;
                    dx = (double)E[k].x1 - F(b->x); dy = (double)E[k].y1 - F(b->y);
                    if (sqrt(dx * dx + dy * dy) < 0.001) { found = k; break; }
                }
                if (found >= 0) E[found].active = 0;
                else {                                        /* FUN_00517dc4 */
                    int k = 0;
                    while (k < ne && E[k].active) k++;
                    if (k < 0xc30) {
                        E[k].idx = cur_idx;
                        E[k].x0 = F(a->x); E[k].y0 = F(a->y);
                        E[k].x1 = F(b->x); E[k].y1 = F(b->y);
                        E[k].active = 1;
                        if (ne <= k) ne = k + 1;
                    }
                }
            }
            if (end) break;
            cur_idx = recidx;                                 /* index counted from record 12 */
            rec[0] = *r;
            cnt = 1;
        } else if (r->tag == EG_T_LINE || r->tag == EG_T_POINT) {
            if (cnt < 4) rec[cnt++] = *r;
        }
        recidx++;
    }
    /* FUN_005182cc */
    int bad = 0;
    for (int k = 0; k < ne; k++)
        if (E[k].active && !spike_connected(f, &E[k])) { bad = 1; break; }
    if (bad) {
        /* FUN_00518064: markers (block 0x25, max 500) */
        if (n_markers) {
            for (int k = 0; k < ne; k++) {
                if (!E[k].active) continue;
                if (*n_markers > 499) break;
                if (markers_xy) { markers_xy[2 * *n_markers] = E[k].x0; markers_xy[2 * *n_markers + 1] = E[k].y0; }
                (*n_markers)++;
            }
        }
        /* FUN_00517e68: zero tags at the stored index (absolute record index in the file -
           the index was counted from record 12, kept as in the binary), then compact. */
        for (int k = 0; k < ne; k++) {
            if (!E[k].active) continue;
            int p = E[k].idx;
            if (p < 0 || p + 2 >= f->n) continue;
            if (f->r[p].tag == EG_T_MOVE) {
                f->r[p].tag = 0;
                if (f->r[p + 1].tag != EG_T_MOVE) {
                    f->r[p + 1].tag = 0;
                    if (f->r[p + 2].tag != EG_T_MOVE) f->r[p + 2].tag = 0;
                }
            }
        }
        int w = 0;
        for (int i = 0; i < f->n; i++) if (f->r[i].tag != 0) f->r[w++] = f->r[i];
        f->n = w;
    }
    free(E);
    return EG_OK;
}

/* ------------------------------------------------------------------ FUN_0050cc2c/FUN_0050cddc */
int eg_block_append(EgRecs *layer, const EgRecs *slot)
{
    if (slot->n <= 12) return EG_OK;                          /* FUN_0050cf5c: size > 0xc */
    int start = layer->n;                                     /* [parent-0xc4] */
    float x0 = 10000.0f, y0 = 10000.0f, x1 = -10000.0f, y1 = -10000.0f; /* 0x461c4000 */
    for (int i = 0; i < slot->n; i++) {
        const EgRec *r = &slot->r[i];
        if (r->tag == 0x0a || r->tag == 0x0c || r->tag == 0x0b || r->tag == 0x1c) {
            if (F(r->x) < x0) x0 = F(r->x);
            if (F(r->y) < y0) y0 = F(r->y);
            if (x1 < F(r->x)) x1 = F(r->x);
            if (y1 < F(r->y)) y1 = F(r->y);
        }
        if (eg_recs_push_rec(layer, r)) return EG_ERR_MEM;
    }
    int end = layer->n;                                       /* [parent-200] */
    if (end > 0 && layer->r[end - 1].tag == EG_T_END) end--;  /* 0x50cf75 */
    /* FUN_0050cddc: patch records start..start+3 */
    EgRec *h = &layer->r[start];
    h[0].x = x0; h[0].y = y0; h[0].z = (end - start) + 1;
    h[1].x = x1; h[1].y = y1; h[1].z = x0;
    h[3].y = y0;
    layer->n = end;                                           /* tag 9 written at position end */
    return eg_recs_push(layer, EG_T_END, 0.0, 0.0, 0.0);
}

/* ------------------------------------------------------------------ FUN_0050d074 order */
int eg_block_order(int KAbraeumZuerst, int order[16])
{
    int flags = (KAbraeumZuerst ? 0 : 0x10) + 1;              /* FUN_0050d1a4 + 1 */
    int n = 0;
    for (int s = 15; s >= 0; s--) {                           /* (flags & 8) == 0: descending */
        if ((flags & 0x10) && s == 10) continue;
        order[n++] = s;
    }
    if (flags & 0x10) order[n++] = 10;
    return n;
}

/* ------------------------------------------------------------------ FUN_00499380(..., 5, ...) */
int eg_final_copy(const EgRecs *layer, EgRecs *out)
{
    for (int i = 0; i < layer->n; i++) {
        if (layer->r[i].tag == 0) continue;                   /* p_460000.c 43189 */
        if (eg_recs_push_rec(out, &layer->r[i])) return EG_ERR_MEM;
    }
    return EG_OK;
}

/* ------------------------------------------------------------------ depth cuts */
/* FUN_004fa354 (one layer): append to out */
static int layer_pass(const EgRecs *src, float zmax, float ratio, float spiegelt, EgRecs *out)
{
    EgRec prev; prev.tag = 0; prev.x = prev.y = prev.z = 0.0;
    float zr = F((double)zmax * ratio);                       /* param_4 * param_5 */
#define EMIT(rr) do { EgRec e_ = (rr); \
        if (e_.tag == EG_T_POINT || e_.tag == 0x1b) e_.z = F(F(e_.z) + (double)spiegelt); \
        if (eg_recs_push_rec(out, &e_)) return EG_ERR_MEM; } while (0)
    for (int i = 0; i < src->n; i++) {
        EgRec r = src->r[i];
        if (r.tag == EG_T_LINE) {
            if (prev.tag == EG_T_MOVE) {
                EMIT(prev);                                   /* flag [4fa5bc-0x25] is always 1 */
                prev.tag = EG_T_POINT;
                prev.z = zr;
                EMIT(prev);
            }
            r.tag = EG_T_POINT;
            r.z = zr;
            EMIT(r);
            prev = r;
        } else if (r.tag == EG_T_MOVE) {
            prev = r;
        } else if (r.tag == EG_T_POINT) {
            if (prev.tag == EG_T_MOVE) EMIT(prev);
            r.z = F(F(r.z) * (double)ratio);
            EMIT(r);
            prev = r;
        }
    }
#undef EMIT
    return EG_OK;
}

int eg_depth_cut(const EgRecs *slot, int layers, char aufteil, double ramp_deg, double spiegelt,
                 const EgTool *t, EgRecs *out)
{
    if (slot->n < 12 || slot->r[9].tag != EG_T_TOOL9B) return EG_ERR_HEADER; /* 0x4fa92a */
    /* FUN_004fa8b8: zmin / zmax over tags 0x0a..0x0c */
    float zmin = -10000.0f, zmax = t->D;
    for (int i = 0; i < slot->n; i++) {
        const EgRec *r = &slot->r[i];
        if (r->tag < 10 || r->tag > 12) continue;
        if (F(r->z) < zmin) zmin = F(r->z);
        if (zmax < F(r->z)) zmax = F(r->z);
    }
    if (zmin <= -10000.0f) zmin = 0.0f;
    /* header with rec 9 y = 1.0 (0x4fa932) */
    for (int i = 0; i < 12; i++) {
        EgRec h = slot->r[i];
        if (i == 9) h.y = 1.0;
        if (eg_recs_push_rec(out, &h)) return EG_ERR_MEM;
    }
    /* FUN_004fa5bc */
    float a_spant = F(ramp_deg), S = F(spiegelt);
    if (t->angle < 1.0f) aufteil = 'L';                       /* 0x4fa5f4 */
    int steep = 89.9 > a_spant;                               /* 0x4fa60a */
    int n = layers > 999 ? 999 : layers;
    if (zmin > 0.0f || zmin <= -10000.0f) zmin = 0.0f;
    float cur = zmin < 0.0f ? zmin : 0.0f;
    if (n > 0) {
        float step = F(((double)zmax - zmin) / n);
        for (int k = 1; k <= n; k++) {
            float prevc = cur;
            /* FUN_004fa130 */
            if (aufteil == 'L') cur = F((double)k * step + zmin);
            else if (aufteil == 'V') {
                float ta = tan_f(F(DEG2RAD * t->angle));
                float A = F(((double)t->D * ta + t->rt) * t->D / n);
                float c = F((double)t->rt / 2.0 / ta);
                cur = F(sqrt(((double)(k - 1) * A + A) / ta + (double)c * c) - (double)t->rt / 2.0 / ta);
            }
            float ratio = F((double)cur / zmax);
            float dz = F((double)cur - prevc);
            float tr = F((double)dz / tan_f(F(DEG2RAD * a_spant)));
            if (steep && tr > 0.01) return EG_ERR_NOT_DECODED; /* FUN_004f9824 ramp */
            int rc = layer_pass(slot, zmax, ratio, S, out);   /* + FUN_00499380(...,0,0) append */
            if (rc) return rc;
        }
        return EG_OK;
    }
    /* layers <= 0: step pushed as 0.0 (0x4faabc) */
    for (;;) {
        float prevc = cur;
        cur = F((double)cur + 0.0);
        if (cur > zmax || (double)zmax - cur <= 0.002) cur = zmax;
        else return EG_ERR_HANG;                              /* cur never advances */
        float ratio = F((double)cur / zmax);
        float dz = F((double)cur - prevc);
        float tr = F((double)dz / tan_f(F(DEG2RAD * a_spant)));
        if (steep && tr > 0.01) return EG_ERR_NOT_DECODED;
        int rc = layer_pass(slot, zmax, ratio, S, out);
        if (rc) return rc;
        if (!((double)cur <= (double)zmax - 0.001)) break;
    }
    return EG_OK;
}

/* ================================================================== ordering (follow-up) */

/* FUN_00510c6c @69417 (via FUN_00510f10 @69558): contour index */
int eg_contour_index(const EgRecs *in, float thr, EgContourIdx **pidx, EgBox *box)
{
    *pidx = NULL;
    if (in->n < 4) return EG_ERR_NODATA;                      /* 0x44f (69457) */
    EgContourIdx *ix = (EgContourIdx *)calloc((size_t)in->n + 1, sizeof(EgContourIdx));
    if (!ix) return EG_ERR_MEM;
    float bx0 = 100000.0f, by0 = 100000.0f, bx1 = -100000.0f, by1 = -100000.0f; /* local_38 */
    float ox0 = 100000.0f, oy0 = 100000.0f, ox1 = -100000.0f, oy1 = -100000.0f; /* +0x3b */
    float lx = 0, ly = 0;                                     /* local_18 / local_1c */
    int cnt = 0, in_c = 0;
    EgContourIdx e;
    memset(&e, 0, sizeof e);
    e.thr = thr;
#define UNION() do {                                            \
        if (bx0 <= ox0) ox0 = bx0;                                  \
        if (ox1 <= bx1) ox1 = bx1;                                  \
        if (by0 <= oy0) oy0 = by0;                                  \
        if (!(by1 < oy1)) oy1 = by1;                                \
    } while (0)
    for (int i = 0; i < in->n; i++) {
        const EgRec *r = &in->r[i];
        if (r->tag == EG_T_MOVE) {
            if (!in_c) {
                in_c = 1;
                e.thr = thr; e.start = i; e.sx = F(r->x); e.sy = F(r->y);
            } else {
                e.ymid = F(((double)by0 + by1) / 2.0);
                UNION();                                      /* FUN_004c4570 */
                e.thr = thr;
                e.end = i - 1;
                if (e.start < i - 1) {
                    e.ex = lx; e.ey = ly;
                    e.xmin = bx0; e.ymin = by0; e.xmax = bx1; e.ymax = by1;
                    ix[cnt++] = e;                            /* FUN_00510b60 */
                    memset(&e, 0, sizeof e);
                }
                bx0 = by0 = 100000.0f; bx1 = by1 = -100000.0f;
                e.start = i; e.thr = thr; e.sx = F(r->x); e.sy = F(r->y);
            }
            int k = (int)nearbyint(F(r->z));                  /* 69499 */
            e.type = (k == -5) ? -5 : (k == -4) ? -4 : 0;
        } else if (r->tag == 0x80) {
            if (F(r->z) > 0.0f) thr = F(0.7 * F(r->z));       /* 0x510e05 */
        } else {
            lx = F(r->x); ly = F(r->y);
        }
        if (r->tag == EG_T_LINE || r->tag == EG_T_POINT || r->tag == EG_T_MOVE) {
            float x = F(r->x), y = F(r->y);
            if (x < bx0) bx0 = x;
            if (bx1 < x) bx1 = x;
            if (y < by0) by0 = y;
            if (by1 < y) by1 = y;
        }
    }
    e.ymid = F(((double)by0 + by1) / 2.0);
    UNION();
#undef UNION
    e.end = in->n - 1;
    e.ex = lx; e.ey = ly;
    e.xmin = bx0; e.ymin = by0; e.xmax = bx1; e.ymax = by1;
    ix[cnt] = e;
    if (!(in->n - 1 <= e.start)) cnt++;                       /* 69542: else count-- */
    if (box) { box->xmin = ox0; box->ymin = oy0; box->xmax = ox1; box->ymax = oy1; }
    *pidx = ix;
    return cnt;
}

/* FUN_00527390 @86130: parity point-in-contour of (px,py) in seed records s[0..m-1] */
static int seed_contains(const EgRec *s, int m, float px, float py0)
{
    double py = F(py0);
    for (;;) {
        int count = 0, hit = 0;
        EgRec prev = s[0];
        for (int i = 1; i <= m - 1; i++) {
            if ((double)F(prev.x) == py) { hit = 1; break; } /* 0x5273e0: compares x (as in binary) */
            const EgRec *c = &s[i];
            if ((double)F(c->y) == py) { hit = 1; break; }
            if (c->tag == EG_T_LINE || c->tag == EG_T_POINT) {
                double d = (double)F(c->y) - F(prev.y);
                if (d != 0.0) {
                    double t = (py - F(prev.y)) / d;
                    if (t > 0.0 && t < 1.0) {
                        double xc = ((double)F(c->x) - F(prev.x)) * t + F(prev.x);
                        if (xc < px) count++;                 /* 0x527478 */
                    }
                }
            }
            prev = *c;
        }
        if (!hit) return (count & 1) != 0;
        py = (double)F(0.0002 + py);                          /* 0x5274ae */
    }
}

/* FUN_00527280: copy records start..end with tags 0x0a..0x0c */
static int copy_contour(EgRecs *out, const EgRecs *in, int start, int end)
{
    for (int i = start; i <= end && i < in->n; i++) {
        int t = in->r[i].tag;
        if (t >= 10 && t <= 12) if (eg_recs_push_rec(out, &in->r[i])) return EG_ERR_MEM;
    }
    return EG_OK;
}

typedef struct {
    const EgRecs *in;
    EgContourIdx *ix;
    int n;
    unsigned char *used;
    int last_arg;
    EgRecs *out;  /* VekARH01 */
} BlkSt;

/* FUN_005274e8 @86199 for the cell (x, y, cw, ch) */
static int block_cell(BlkSt *st, float x, float cw, float y, float ch)
{
    float xr = F((double)x + cw), yb = F((double)y - ch);     /* [ebp-0x18], [ebp-0x1c] */
    for (;;) {
        int found = 0, k = 0;
        float best = 1e20f;
        for (int i = 0; i < st->n; i++) {
            if (st->used[i]) continue;
            const EgContourIdx *c = &st->ix[i];
            if (c->type == st->last_arg) continue;            /* 0x5275a7 */
            if (c->ymax < yb) continue;                       /* 0x5275ad */
            if (c->xmin > xr) continue;                       /* 0x5275b9 */
            double dx = (double)x - c->xmin, dy = (double)y - c->ymax;
            float d = F(sqrt(dx * dx + dy * dy));
            if (d < best) { found = 1; k = i; best = d; }
        }
        if (!found) return EG_OK;
        const EgContourIdx *sd = &st->ix[k];
        st->used[k] = 0xff;
        int m = sd->end - sd->start + 1;
        const EgRec *srec = &st->in->r[sd->start];
        for (int j = 0; j < st->n; j++) {
            if (st->used[j]) continue;
            const EgContourIdx *c = &st->ix[j];
            if (sd->xmin <= c->xmin && sd->ymin <= c->ymin && c->xmax <= sd->xmax && c->ymax <= sd->ymax) {
                const EgRec *p = &st->in->r[c->start];
                if (seed_contains(srec, m, F(p->x), F(p->y))) {
                    if (copy_contour(st->out, st->in, c->start, c->end)) return EG_ERR_MEM;
                    st->used[j] = 0xff;
                }
            }
        }
        if (copy_contour(st->out, st->in, sd->start, sd->end)) return EG_ERR_MEM;
        if (eg_recs_push(st->out, 0x80, 0.0, 0.0, 0.0)) return EG_ERR_MEM; /* 86322 */
    }
}

int eg_blocks(const EgRecs *vekar1, int mode, int last_arg, EgRecs **pblocks, char **ptypes,
              int *nblocks)
{
    *pblocks = NULL; *ptypes = NULL; *nblocks = 0;
    EgContourIdx *ix = NULL;
    EgBox ob;
    int n = eg_contour_index(vekar1, 0.0f, &ix, &ob);        /* FUN_00510f10(..., 0.0) */
    if (n < 0) return n;
    BlkSt st;
    st.in = vekar1; st.ix = ix; st.n = n; st.last_arg = last_arg;
    st.used = (unsigned char *)calloc((size_t)n + 1, 1);
    EgRecs h01 = { NULL, 0, 0 };
    st.out = &h01;
    if (!st.used) { free(ix); return EG_ERR_MEM; }
    int rc = EG_OK;
    float x0 = F(ob.xmin), y0 = F(ob.ymin), x1 = F(ob.xmax), y1 = F(ob.ymax);
    if (mode == 0 || mode == 1) {                             /* 86436 / 86456 */
        float cw = F(((double)x1 - x0) / 20.0); if (cw < 2.0f) cw = 2.0f;
        float ch = F(((double)y1 - y0) / 20.0); if (ch < 2.0f) ch = 2.0f;
        if (mode == 0) {
            float y = y1;
            do {
                float x = x0;
                do {
                    if ((rc = block_cell(&st, x, cw, y, ch))) break;
                    x = F((double)x + cw);
                } while (!(x1 < x));
                y = F((double)y - ch);
            } while (!rc && y0 <= y);
        } else {
            float x = x0;
            do {
                float y = y1;
                do {
                    if ((rc = block_cell(&st, x, cw, y, ch))) break;
                    y = F((double)y - ch);
                } while (!(y < y0));
                x = F((double)x + cw);
            } while (!rc && x <= x1);
        }
    } else if (mode == 2) {
        rc = block_cell(&st, x0, F((double)x1 - x0), y1, F((double)y1 - y0));
    }
    if (!rc && last_arg != 0)                                 /* FUN_005277f0 */
        for (int i = 0; i < n && !rc; i++)
            if (!st.used[i]) rc = copy_contour(&h01, vekar1, ix[i].start, ix[i].end);
    /* FUN_00527c5c: split VekARH01 at 0x80 */
    EgRecs *bl = NULL;
    char *ty = NULL;
    int nb = 0, i = 0;
    while (!rc && i < h01.n) {
        EgRecs b = { NULL, 0, 0 };
        int sep = 0, any = 0;
        for (; i < h01.n; i++) {
            if (h01.r[i].tag == 0x80) { sep = 1; any = 1; i++; break; }
            if (h01.r[i].tag >= 10 && h01.r[i].tag <= 12) any = 1;
            if (eg_recs_push_rec(&b, &h01.r[i])) { rc = EG_ERR_MEM; break; }
        }
        if (rc || !any) { eg_recs_free(&b); break; }
        EgRecs *nbl = (EgRecs *)realloc(bl, (size_t)(nb + 1) * sizeof(EgRecs));
        char *nty = (char *)realloc(ty, (size_t)(nb + 1));
        if (!nbl || !nty) { free(nbl ? nbl : bl); free(nty ? nty : ty); eg_recs_free(&b); bl = NULL; ty = NULL; nb = 0; rc = EG_ERR_MEM; break; }
        bl = nbl; ty = nty;
        bl[nb] = b;
        ty[nb] = sep ? 'V' : 'E';                             /* *param_4 = 0 at 0x80 (86571) */
        nb++;
    }
    eg_recs_free(&h01);
    free(st.used);
    free(ix);
    if (rc) { for (int k = 0; k < nb; k++) eg_recs_free(&bl[k]); free(bl); free(ty); return rc; }
    *pblocks = bl; *ptypes = ty; *nblocks = nb;
    return EG_OK;
}

/* ------------------------------------------------------------------ FUN_0051bff0 re-sort */
static float link_len_add(float acc, int *has, float *px, float *py, const EgContourIdx *c)
{
    if (*has) {
        double dx = (double)c->sx - *px, dy = (double)c->sy - *py;
        acc = F(sqrt(dx * dx + dy * dy) + acc);
    }
    *has = 1; *px = c->ex; *py = c->ey;
    return acc;
}

/* variants of FUN_0051bbfc @77982; order[] receives the contour indices, returns the length */
static float resort_variant(int v, const EgContourIdx *ix, int n, const EgBox *ob, int *order)
{
    unsigned char *used = (unsigned char *)calloc((size_t)n + 1, 1);
    float len = 0.0f, px = 0, py = 0;
    int has = 0, no = 0;
    if (!used) return 3.0e38f;
    if (v == 1) {                                             /* FUN_0051af9c / FUN_0051acd8 */
        float x0 = F(ob->xmin), y0 = F(ob->ymin), x1 = F(ob->xmax), y1 = F(ob->ymax);
        float cw = F(((double)x1 - x0) / 20.0); if (cw < 2.0f) cw = 2.0f;
        float ch = F(((double)y1 - y0) / 20.0); if (ch < 2.0f) ch = 2.0f;
        float y = y1;
        do {
            float x = x0;
            do {
                for (;;) {
                    int f = 0, k = 0;
                    float best = 10000.0f;
                    for (int i = 0; i < n; i++) {
                        if (used[i]) continue;
                        const EgContourIdx *c = &ix[i];
                        if (!(c->sx <= F((double)x + cw))) continue;
                        if (!(F((double)y - ch) <= c->sy)) continue;
                        double dx = (double)c->sx - x, dy = (double)c->sy - y;
                        float d = F(sqrt(dx * dx + dy * dy));
                        if (d < best) { f = 1; k = i; best = d; }
                    }
                    if (!f) break;
                    len = link_len_add(len, &has, &px, &py, &ix[k]);
                    order[no++] = k;
                    used[k] = 0xff;
                }
                x = F((double)x + cw);
            } while (x <= x1);
            y = F((double)y - ch);
        } while (y0 <= y);
    } else {
        float rx = F(ob->xmin), ry = F(ob->ymax);             /* refs [-0x102c],[-0x1034] */
        for (;;) {
            int f = 0, k = 0;
            float best = 10000.0f;
            for (int i = 0; i < n; i++) {
                if (used[i]) continue;
                float d;
                if (v == 2) d = F((double)ix[i].sy - ry);     /* FUN_0051b154(0) */
                else if (v == 3) d = F((double)ix[i].sx - rx);/* FUN_0051b154(1) */
                else {                                        /* FUN_0051b48c / FUN_0051b768 */
                    double dx = (double)ix[i].sx - rx, dy = (double)ix[i].sy - ry;
                    d = F(sqrt(dx * dx + dy * dy));
                }
                if (d < best) { best = d; f = 1; k = i; }
            }
            if (!f) break;
            len = link_len_add(len, &has, &px, &py, &ix[k]);
            if (v == 2) ry = ix[k].sy;
            else if (v == 3) rx = ix[k].sx;
            else if (v == 5) { rx = ix[k].ex; ry = ix[k].ey; }
            order[no++] = k;
            used[k] = 0xff;
        }
    }
    free(used);
    return len;
}

/* FUN_00511194 @69705: does the link (cx,cy)->(sx,sy) cross a finish segment? */
static int link_collides(const EgRecs *fin, float sx, float sy, float cx, float cy)
{
    if (fin->n < 2) return 0;
    float ax = F((double)sx - cx), ay = F((double)sy - cy);   /* [ebp-8], [ebp-0xc] */
    EgRec p = fin->r[0];
    for (int i = 1; i <= fin->n - 1; i++) {
        const EgRec *r = &fin->r[i];
        if (r->tag == EG_T_MOVE) { p = *r; continue; }
        if (r->tag != EG_T_LINE && r->tag != EG_T_POINT) continue;
        float bx = F((double)F(p.x) - F(r->x)), by = F((double)F(p.y) - F(r->y));
        float den = F((double)ay * bx - (double)ax * by);
        if (den != 0.0f) {
            float t = F((((double)F(p.y) - cy) * bx - ((double)F(p.x) - cx) * by) / den);
            if (!(-0.001 > t) && !(1.001 < t)) {
                float u = F((((double)F(p.x) - cx) * ay - ((double)F(p.y) - cy) * ax) / den);
                if (!(-0.001 > u) && !(1.001 < u)) return 1;
            }
        }
        p = *r;
    }
    return 0;
}

/* FUN_00510f84: write one pass; with link the leading 0x0b becomes 0x0a (or is dropped when the
   next record is 0x0c) */
static int write_pass(EgRecs *out, const EgRecs *in, int start, int end, int link)
{
    for (int i = start; i <= end && i < in->n; i++) {
        EgRec r = in->r[i];
        if (r.tag < 10 || r.tag > 12) continue;
        if (link) {
            link = 0;
            int nt = (i + 1 < in->n) ? in->r[i + 1].tag : 0;
            if (nt == 10 || nt == 11) { r.tag = EG_T_LINE; if (eg_recs_push_rec(out, &r)) return EG_ERR_MEM; }
            continue;
        }
        if (eg_recs_push_rec(out, &r)) return EG_ERR_MEM;
    }
    return EG_OK;
}

int eg_rough_resort(const EgRecs *slot10, const EgRecs *finish, double stepover, EgRecs *out,
                    int *variant)
{
    if (variant) *variant = -1;
    for (int i = 0; i < slot10->n && i < 12; i++)
        if (eg_recs_push_rec(out, &slot10->r[i])) return EG_ERR_MEM;
    if (slot10->n <= 0xe) {                                   /* 78203 */
        for (int i = 12; i < slot10->n; i++) if (eg_recs_push_rec(out, &slot10->r[i])) return EG_ERR_MEM;
        return EG_OK;
    }
    /* FUN_0051bef8: passes -> VekAR10 */
    EgRecs v10 = { NULL, 0, 0 };
    for (int i = 12; i < slot10->n; i++) if (eg_recs_push_rec(&v10, &slot10->r[i])) { eg_recs_free(&v10); return EG_ERR_MEM; }
    /* FUN_0051bbfc */
    EgContourIdx *ix = NULL;
    EgBox ob;
    int n = eg_contour_index(&v10, 0.0f, &ix, &ob);
    EgRecs sorted = { NULL, 0, 0 };
    int rc = EG_OK, chosen = 0;
    if (n >= 0) {
        int *ord = (int *)malloc(sizeof(int) * 6 * ((size_t)n + 1));
        if (!ord) rc = EG_ERR_MEM;
        else {
            float L[6];
            for (int v = 1; v <= 5; v++) L[v] = resort_variant(v, ix, n, &ob, ord + (size_t)v * (n + 1));
            /* FUN_0051bb20: original order length */
            float l0 = 0.0f; EgRec prev; prev.tag = 0; prev.x = prev.y = prev.z = 0;
            for (int i = 0; i < v10.n; i++) {
                if (v10.r[i].tag == EG_T_MOVE && prev.tag != 0) l0 = F(dist2d(&prev, &v10.r[i]) + (double)l0);
                prev = v10.r[i];
            }
            float best = L[1]; chosen = 1;
            for (int v = 2; v <= 5; v++) if (L[v] < best) { best = L[v]; chosen = v; }
            if (l0 < best) chosen = 0;
            if (chosen == 0) { for (int i = 0; i < v10.n && !rc; i++) rc = eg_recs_push_rec(&sorted, &v10.r[i]); }
            else {
                int *o = ord + (size_t)chosen * (n + 1);
                for (int k = 0; k < n && !rc; k++) rc = copy_contour(&sorted, &v10, ix[o[k]].start, ix[o[k]].end);
            }
            free(ord);
        }
        free(ix);
    } else {
        for (int i = 0; i < v10.n && !rc; i++) rc = eg_recs_push_rec(&sorted, &v10.r[i]);
    }
    eg_recs_free(&v10);
    if (variant) *variant = chosen;
    /* FUN_0051170c -> FUN_00511310(VekAR10, slot 10, finish, forward = 1, connect = 1, ...) */
    if (!rc) {
        float thr = F(0.7 * stepover);
        int m = eg_contour_index(&sorted, thr, &ix, &ob);
        if (m > 0) {
            unsigned char *used = (unsigned char *)calloc((size_t)m + 1, 1);
            if (!used) rc = EG_ERR_MEM;
            float cx = ix[0].sx, cy = ix[0].sy;
            int first = 1;
            while (!rc) {
                int f = 0, k = 0;
                float best = 1e20f;
                for (int i = 0; i < m; i++) {                 /* FUN_005110dc, forward */
                    if (used[i]) continue;
                    double dx = (double)cx - ix[i].sx, dy = (double)cy - ix[i].sy;
                    float d = F(sqrt(dx * dx + dy * dy));
                    if (d < best) { best = d; k = i; f = 1; }
                }
                if (!f) break;
                const EgContourIdx *c = &ix[k];
                const int connect = 1;                        /* param_5 = 1 from FUN_0051bff0 */
                /* 0x511547..0x511588: first, or (dist < thr || connect) && no collision */
                if (first || ((best < c->thr || connect) && !link_collides(finish, c->sx, c->sy, cx, cy))) {
                    rc = write_pass(out, &sorted, c->start, c->end, !first);
                    first = 0;
                    used[k] = 0xff;
                    cx = c->ex; cy = c->ey;
                } else {
                    first = 1;
                    for (int i = 0; i < m; i++) if (!used[i]) { cx = ix[i].sx; cy = ix[i].sy; break; }
                }
            }
            free(used);
        }
        free(ix);
    }
    eg_recs_free(&sorted);
    return rc;
}

#undef BlkSt
#undef Cell
#undef F
#undef LoadSt
#undef OvSt
#undef Seg9
#undef Spike
#undef block_cell
#undef copy_contour
#undef corner_metric
#undef dist2d
#undef eg_recs_free
#undef eg_recs_push
#undef eg_recs_push_rec
#undef eg_recs_reserve
#undef field_is_int
#undef layer_pass
#undef le_word
#undef link_collides
#undef link_len_add
#undef load_emit
#undef load_flush_pend
#undef outcode
#undef ov_mark
#undef parity_inside
#undef prep_flush
#undef probe_x
#undef rd32
#undef reduce_contour
#undef resort_variant
#undef reverse_one
#undef rotate_out
#undef seed_contains
#undef slot_ok
#undef spike_connected
#undef start_one
#undef tan_f
#undef wr32
#undef write_pass
#undef DEG2RAD
#undef EMIT
#undef UNION

/* ================================================================================================================== */
/* the engine driver: FUN_00529e94 -> FUN_0052919c -> FUN_00528784 / FUN_00528dac / FUN_00528104 (formats/engrave_engine.md 1, 7) */

#define EG_NSLOT 16
typedef struct {
    const EgEstDat *e;
    EgToolRow rows[10];
    EgSlotInfo si;
    double W;
    EgRBox gbox;
    EgRecs slot[EG_NSLOT];     /* the slot files (FUN_00499f80(n)): 12 header records + moves */
    char gravur;               /* GravurTyp of the current block */
    int err;                   /* the engine's error *(int*)PTR_DAT_0052f230 */
    char msg[160];
} EgEng;

static void eng_fail(EgEng *g, int code, const char *what) {
    if (!g->err) g->err = code ? code : -1;
    if (!g->msg[0]) snprintf(g->msg, sizeof g->msg, "engrave engine: %s (code %d)", what, code);
}
static int recs_append(EgRecs *dst, const EgRecs *src) {   /* FUN_00499380(src, dst, 0, 0): append every record but tag 0 */
    for (int i = 0; i < src->n; i++)
        if (src->r[i].tag && !eg_recs_add(dst, src->r[i].tag, src->r[i].x, src->r[i].y, src->r[i].z)) return 0;
    return 1;
}

/* FUN_00528dac (p_4b0000.c l.87144-87300): one block */
static void eng_block(EgEng *g, const EgRecs *v1) {
    const EgEstDat *e = g->e;
    const int gegen = e->conventional != 0;          /* block 7 +0x14, frame -0x58 */
    EgRecs vs = { 0 }; const EgRecs *src = v1;
    if (e->start_point > 1) {                        /* FUN_00519380(VekAR1, Gegenlauf ^ 1, KStartpunkt) */
        int rc = eg_startpoint(v1, &vs, gegen ^ 1, e->start_point);
        if (rc) { eng_fail(g, rc, "start point"); return; }
        src = &vs;
    }
    /* FUN_005041a4(VekAR1, Gegenlauf; W, KAbrundungswinkel in rad, 0.62831855) -> FUN_00501550 */
    EgPreOut po; memset(&po, 0, sizeof po);
    int rc = eg_offset_precompute(src, gegen, (float)g->W, (float)(e->round_angle * 0.017453292519943295), 0.62831855f, &po);
    eg_recs_free(&vs);
    if (rc) { eg_pre_free(&po); eng_fail(g, rc, rc == EG_PRE_ERR_NOTDECODED || rc == EG_PRE_ERR_STALE ? "offset precomputation: case NOT DECODED" : "offset precomputation"); return; }
    EgOffsetPre pre; eg_pre_view(&po, &pre);
    for (int s = 0; s <= g->si.last && !g->err; s++) {       /* slot loop l.87216-87270 */
        const EgToolRow *R = &g->rows[s];
        char k = R->corr;
        EgRecs t = { 0 };
        if (k == 'E') { eng_fail(g, -1, "corner tool (KorrekturVerf Ecke) is not ported"); break; }
        if ((k == '3' || k == 'A') && e->contours_on == 'S' && R->angle > 0) {
            /* FUN_00528c9c -> FUN_00517ac8: offset core in mode 0 at L, then FUN_00517594 */
            double L, ratio;
            eg_sharp_spiegel_params(R->R, R->rt, 0.0 /* *extra, measured 0 */, &L, &ratio);
            EgOffsetParams kp = { gegen, 1 };
            rc = eg_offset_core(&pre, &t, NULL, 0, 0.0f, (float)L, (float)g->W, &kp);
            if (rc) { eg_recs_free(&t); eng_fail(g, rc, "offset (sharpening input)"); break; }
            EgRecs o = { 0 };
            rc = eg_sharp_spiegel(&t, &o, k == 'A', R->D, ratio);
            if (rc == 0 && !recs_append(&g->slot[s], &o)) rc = -1;
            eg_recs_free(&o);
            if (rc) { eg_recs_free(&t); eng_fail(g, rc, "3D sharpening"); break; }
        } else if (k == '3' || k == 'A') {
            eng_fail(g, -1, "Grund with a 3D correction (rejected by FUN_00529b50)"); break;
        } else {   /* FUN_0051001c(VekAR1 -> VekAR8, Gegenlauf, 1, colour, R + *extra, W) + append to the slot file */
            rc = eg_offset_retry(&pre, &t, gegen, (float)R->R, (float)g->W);
            if (rc == EG_OFF_NODATA) rc = 0;          /* l.87252: error 100 is reset */
            if (rc) { eg_recs_free(&t); eng_fail(g, rc, "2D offset"); break; }
            recs_append(&g->slot[s], &t);
        }
        eg_recs_free(&t);
    }
    if (!g->err && e->abr != 'K') {                    /* FUN_00528104 */
        if (e->abr != 'Z' && e->abr != 'Y') eng_fail(g, -1, "roughing types other than zigzag / one way are not ported");
        else {
            EgRecs b = { 0 };
            /* the boundary: FUN_0051001c(VekAR1 -> VekAR10, Gegenlauf, 1, colour, maxR, W) */
            rc = eg_offset_retry(&pre, &b, gegen, (float)g->si.maxR, (float)g->W);
            if (rc == EG_OFF_NODATA) rc = 0;
            if (rc) eng_fail(g, rc, "roughing boundary offset");
            else {
                EgRoughParams rp; memset(&rp, 0, sizeof rp);
                rp.verf = e->abr; rp.gravur_v = g->gravur == 'V'; rp.gbox = g->gbox;
                rp.angle = (float)(e->abr_angle * 0.017453292519943295);   /* FUN_00529b50 0x529dc7: deg -> rad */
                rp.overlap = (float)e->overlap; rp.rt = g->rows[g->si.last].rt;
                rp.kzv = e->connect_lines; rp.abr_zuerst = e->abr_first; rp.abr_weg_zus = e->abr_merge;
                rp.abr_zeilen_rund = 0;                    /* the rounding tail runs after all blocks (eng_run) */
                rp.e_stack = 0;
                int ee = 0;
                rc = eg_rough_528104(&rp, &b, NULL, &g->slot[g->si.last], &g->slot[10], &ee);
                if (rc) eng_fail(g, rc, rc == EG_ROUGH_NOT_DECODED ? "roughing: case NOT DECODED" : "roughing");
                else if (ee > 0) eng_fail(g, ee, "roughing");
            }
            eg_recs_free(&b);
        }
    }
    eg_pre_free(&po);
}

int eg_engine(const EgEstDat *e, const EgRecs *contours, EgResult *out, char *err, size_t errn) {
    EgEng g; memset(&g, 0, sizeof g); g.e = e;
    memset(out, 0, sizeof *out);
    EgRecs L = { 0 }, V1 = { 0 }, V10 = { 0 }, VR = { 0 }, layer = { 0 }, lg1 = { 0 };
    EgTool lib[10]; memset(lib, 0, sizeof lib);
    int rc;
    /* FUN_004dd3e4: ENGCONT -> working layer, tolerance 0.001 (0x529eea) */
    if ((rc = eg_load_layer(contours, &L, 0.001))) { eng_fail(&g, rc, "input contours"); goto done; }
    /* FUN_00529514: the global box = Grenze box united with the layer box */
    {
        double x0 = e->box[0], y0 = e->box[1], x1 = e->box[2], y1 = e->box[3];
        for (int i = 0; i < L.n; i++) {
            x0 = fmin(x0, L.r[i].x); y0 = fmin(y0, L.r[i].y); x1 = fmax(x1, L.r[i].x); y1 = fmax(y1, L.r[i].y);
        }
        g.gbox.xmin = (float)x0; g.gbox.ymin = (float)y0; g.gbox.xmax = (float)x1; g.gbox.ymax = (float)y1;
    }
    /* tool library (block 0xf) and FUN_00529b50 */
    short sidx[10]; char scor[10];
    for (int i = 0; i < 10; i++) { sidx[i] = -1; scor[i] = '3'; }
    for (int i = 0; i < e->ntools && i < 10; i++) {
        const EgEstTool *t = &e->tool[i];
        snprintf(lib[i].name, sizeof lib[i].name, "%s", t->name);
        lib[i].D = (float)t->depth; lib[i].fly = (float)t->fly; lib[i].rt = (float)t->tip_r; lib[i].angle = (float)t->angle;
        lib[i].r3a = 0; lib[i].Rs = (float)t->shank_r; lib[i].feed = (float)t->feed; lib[i].plunge = (float)t->plunge;
        lib[i].spindle = (float)t->rpm; lib[i].wechsel = (short)t->change;
        sidx[i] = (short)i; scor[i] = t->korr;
    }
    int ok = 0;
    rc = eg_tool_rows(lib, e->ntools, sidx, scor, e->contours_on, e->abr, g.rows, &ok);
    if (!ok) { eng_fail(&g, rc, "tool table check (FUN_00529b50)"); goto done; }
    /* FUN_00528784 */
    g.si = eg_slot_info(g.rows, 0.0);   /* *extra: an uninitialised local of FUN_00529e38; 0 measured (formats/engrave_engine.md 7) */
    if (g.si.err) { eng_fail(&g, g.si.err, "tip radius <= 0.001"); goto done; }
    { EgBox bx = { g.gbox.xmin, g.gbox.ymin, g.gbox.xmax, g.gbox.ymax }; g.W = eg_width_W(&bx, g.si.maxR); }
    /* FUN_00517c20: slot headers (tools of the used rows; slot 10 = the tool of the last row) */
    for (int s = 0; s <= g.si.last; s++) eg_tool_header(s, &lib[g.rows[s].lib], 0x808080, "           ", &g.slot[s]);
    eg_tool_header(10, &lib[g.rows[g.si.last].lib], 0x808080, "           ", &g.slot[10]);
    if ((rc = eg_contour_prep(&L, &V1, e->param_norm, g.W))) { eng_fail(&g, rc, "contour preparation"); goto done; }
    if (V1.n < 4) { eng_fail(&g, 100, "no contours left"); goto done; }
    if ((rc = eg_orient(&V1, &V10, 1))) { eng_fail(&g, rc, "orientation"); goto done; }
    if (e->datacheck && eg_overlap_check(&V10, NULL, NULL)) { eng_fail(&g, 53, "some of the base contours are overlapping"); goto done; }
    if (eg_reverse_needed(e->conventional, e->engrave_type)) {
        if ((rc = eg_reverse(&V10, &VR))) { eng_fail(&g, rc, "reverse"); goto done; }
    } else { VR = V10; V10.r = NULL; V10.n = V10.cap = 0; }
    /* FUN_0052919c: blocks (KBlockweise >= 2) or the whole file */
    g.gravur = e->engrave_type;
    if (e->blockwise < 2) eng_block(&g, &VR);
    else {
        EgRecs *bl = NULL; char *ty = NULL; int nb = 0;
        rc = eg_blocks(&VR, e->blockwise - 2, e->engrave_type == 'V' ? 1 : -4, &bl, &ty, &nb);
        if (rc) eng_fail(&g, rc, "block split");
        for (int b = 0; b < nb && !g.err; b++) {
            if (e->engrave_type != 'V') g.gravur = ty[b];
            eng_block(&g, &bl[b]);
        }
        for (int b = 0; b < nb; b++) eg_recs_free(&bl[b]);
        free(bl); free(ty);
    }
    if (g.err) goto done;
    /* FUN_00528688: re-sort of the roughing (Knachsortieren && !KAbrWegZusammen, C / D / Z) */
    if (e->resort && !e->abr_merge && (e->abr == 'Z' || e->abr == 'C' || e->abr == 'D')) {
        EgRecs o = { 0 }; int var = 0;
        double st = (1.0 - e->overlap / 100.0) * g.rows[g.si.last].rt;
        rc = eg_rough_resort(&g.slot[10], &g.slot[g.si.last], st, &o, &var);
        if (rc) { eg_recs_free(&o); eng_fail(&g, rc, "roughing re-sort"); goto done; }
        eg_recs_free(&g.slot[10]); g.slot[10] = o;
    }
    if (e->abr == 'Z' && e->abr_round && !e->abr_merge) { eng_fail(&g, -1, "KAbrZeilenRund is not wired"); goto done; }
    /* FUN_00518574: spike removal in slots 0..9 */
    for (int s = 0; s < 10; s++) if (g.slot[s].n > 12) eg_spike_filter(&g.slot[s], NULL, NULL);
    /* FUN_005299e8: depth cuts */
    if (e->spant_on) {
        for (int s = 0; s <= 10; s++) {
            if (g.slot[s].n < 13) continue;
            const EgEstTool *t = s == 10 ? NULL : (s <= g.si.last ? &e->tool[g.rows[s].lib] : NULL);
            int layers = t ? t->layers : e->abr_layers; char split = t ? t->split : e->abr_split; double ramp = t ? t->ramp : e->abr_ramp;
            if (!(layers > 1 || ramp < 90 || e->spant_spiegel > 0)) continue;
            EgRecs o = { 0 };
            const EgTool *lt = &lib[g.rows[s == 10 ? g.si.last : s].lib];
            rc = eg_depth_cut(&g.slot[s], layers, split, ramp, e->spant_spiegel, lt, &o);
            if (rc) { eg_recs_free(&o); eng_fail(&g, rc, rc == EG_ERR_NOT_DECODED ? "depth cut ramp: NOT DECODED" : "depth cuts"); goto done; }
            eg_recs_free(&g.slot[s]); g.slot[s] = o;
        }
    }
    /* FUN_0050d1a4 / FUN_0050d074: slot order, block headers; FUN_00529884 -> FUN_00499380: ENGTOOLP.LG1 */
    {
        int order[16], n = eg_block_order(e->abr_first, order);
        for (int i = 0; i < n; i++) if (g.slot[order[i]].n > 12) eg_block_append(&layer, &g.slot[order[i]]);
        eg_final_copy(&layer, &lg1);
    }
    /* engrave.dll reader (FUN_18005ddf0 / FUN_18005ccc0): per tool block (0x01 .. 0x09) its motion records */
    for (int i = 0; i < lg1.n; i++) {
        int tg = lg1.r[i].tag;
        if (tg == 0x01) {
            out->tp = realloc(out->tp, (size_t)(out->n + 1) * sizeof *out->tp);
            memset(&out->tp[out->n], 0, sizeof out->tp[0]); out->n++;
        } else if (out->n && (tg == 0x07)) out->tp[out->n - 1].tool = (int)lg1.r[i].z;
        else if (out->n && tg >= 0x0a && tg <= 0x0d) eg_recs_add(&out->tp[out->n - 1].rec, tg, lg1.r[i].x, lg1.r[i].y, lg1.r[i].z);
    }
done:
    eg_recs_free(&L); eg_recs_free(&V1); eg_recs_free(&V10); eg_recs_free(&VR); eg_recs_free(&layer); eg_recs_free(&lg1);
    for (int s = 0; s < EG_NSLOT; s++) eg_recs_free(&g.slot[s]);
    if (g.err) { seterr(err, errn, g.msg); eg_result_free(out); return 0; }
    return 1;
}
/* ------------------------------------------------------------------------------------------------------------------ */
/* the whole op                                                                                                          */

/* MEASURED (the coaster NCI; Mastercam's NCI writer behind Write3dMove, FUN_18001df60 -> FUN_180204320 / FUN_1801ffe70,
   not decoded): a rapid that changes x / y and goes down is stored as the x / y move at the start height followed by the
   move down, and a move to the point it starts from is not stored (the engine repeats a point where a sharpening run
   restarts, eg_sharp_spiegel; the stored NCI has no zero-length move) */
static void split_rapids(Move **m, int *nm) {
    int n = *nm, k = 0;
    Move *o = malloc((size_t)(2 * n + 1) * sizeof *o);
    for (int i = 0; i < n; i++) {
        const Move *q = &(*m)[i];
        if (k && fabs(q->p.x - o[k - 1].p.x) < 1e-9 && fabs(q->p.y - o[k - 1].p.y) < 1e-9 && fabs(q->p.z - o[k - 1].p.z) < 1e-9) continue;
        if (k && q->rapid && (fabs(q->p.x - o[k - 1].p.x) > 1e-9 || fabs(q->p.y - o[k - 1].p.y) > 1e-9) && q->p.z < o[k - 1].p.z - 1e-9) {
            o[k] = *q; o[k].p.z = o[k - 1].p.z; k++;
        }
        o[k++] = *q;
    }
    free(*m); *m = o; *nm = k;
}

int engrave_generate(const EngraveIn *in, const EgChain *ch, int nch, Move **m, int *nm, char *err, size_t errn) {
    EgEstDat e; EgLink lk;
    *m = NULL; *nm = 0;
    if (!engrave_setup(in, &e, &lk, err, errn)) return 0;
    /* ENGCONT.LG1 (FUN_180052120 / FUN_18005a5c0): per contour 0x0b at the first point, 0x0a for the others, x25.4 for inch */
    EgRecs c = { 0 };
    double lo[2] = { 1e300, 1e300 }, hi[2] = { -1e300, -1e300 };
    for (int i = 0; i < nch; i++)
        for (int k = 0; k < ch[i].n; k++) {
            double x = ch[i].xy[2 * k] * lk.f, y = ch[i].xy[2 * k + 1] * lk.f;
            eg_recs_add(&c, k ? 0x0a : 0x0b, x, y, 0);
            lo[0] = fmin(lo[0], x); lo[1] = fmin(lo[1], y); hi[0] = fmax(hi[0], x); hi[1] = fmax(hi[1], y);
        }
    e.param_norm = nch ? param_norm_gr(hi[0] - lo[0], hi[1] - lo[1]) : 1;
    EgResult r;
    int ok = eg_engine(&e, &c, &r, err, errn);
    eg_recs_free(&c);
    if (!ok) return 0;
    ok = eg_lg1_to_moves(&r, &lk, m, nm);
    eg_result_free(&r);
    if (!ok) { seterr(err, errn, "engrave: unknown record in the engine output (engraveres error 12)"); return 0; }
    split_rapids(m, nm);
    return 1;
}
