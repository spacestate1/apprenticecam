/* engrave.h - Mastercam 2017 Engrave (opcode 416, the MWENGRAVE C-hook engrave.dll) and its toolpath engine EStamp7.exe
 * (HCAM / eStamp, AHSoft), ported from their decompiles. Spec: ../formats/engrave.md (engrave.dll, parameters) and
 * ../formats/engrave_engine.md (the EStamp7.exe engine). Every rule cites its source function; what is not decoded is
 * refused (an error text), never filled in.
 *
 * Layers (as in Mastercam):
 *   1. engrave.dll: the op record's Engrave block S (record +0x12f0) + tool + chains -> engine parameters (EST_DAT.TXT) and
 *      contours (ENGCONT.LG1, mm)                                    engrave_setup()
 *   2. EStamp7.exe: contours + parameters -> toolpath records (ENGTOOLP.LG1, mm)          eg_engine()
 *   3. engrave.dll: the records -> toolpath moves in part units (FUN_180041910)          eg_lg1_to_moves()
 *   4. Mastercam's line / arc filter on the op's NCI when the op's filter is on (cam_filter.c, not called from here).
 * engrave_generate() runs 1-3. */
#ifndef ENGRAVE_H
#define ENGRAVE_H
#include <stddef.h>
#include <stdint.h>
#include "mcam_load.h"      /* Move */

#ifndef EG_REC_DEFINED
#define EG_REC_DEFINED
typedef struct { int tag; double x, y, z; } EgRec;     /* one LG1 / engine work-file record (13 bytes: u8 tag + 3 x f32) */
typedef struct { EgRec *r; int n, cap; } EgRecs;
#endif

/* EST_DAT.TXT [Werkzeuge] section, one engine tool (EStamp7 parser FUN_005229b0 -> tool library record, 0x8c bytes) */
typedef struct {
    char name[41];          /* LName (lib +1) */
    double depth;           /* LEintauchT (lib +0x2a) mm: cut depth below the top */
    double fly;             /* LFlugHoehe (lib +0x2e) mm: feed plane above the top */
    double tip_r;           /* LFRadius (lib +0x32) mm */
    double angle;           /* LFWinkel (lib +0x36) deg: half angle of the V */
    double shank_r;         /* LFSchaft (lib +0x3e) mm */
    double feed, plunge, rpm;   /* LVorschub / LEintVorschZ / LSpindelDreh */
    int change;             /* LWechselStat */
    char korr;              /* KorrekturVerf: '2' 2D, '3' 2D3D, 'A' 3D, 'E' Ecke */
    int layers;             /* SpantSchichtZ */
    char split;             /* SpantAufteil: 'L' equal, 'V' constant volume */
    double ramp;            /* SpantEintWi deg (90 = vertical) */
} EgEstTool;

typedef struct {            /* EST_DAT.TXT [Parameter] + [Berechnung] (engrave.dll FUN_18004f870 writes them) */
    int bahnopt;            /* Bahnoptimierung */
    double bo_min_dist;     /* BOMindestausd (mm) */
    int bo_min_pts, bo_close, bo_vec_res, circle_res, spline_res;
    double dxf_scale;
    double round_angle;     /* KAbrundungswinkel deg (20 Sharp, 150 None) */
    int conventional;       /* KGegenlauffraesen */
    double overlap;         /* KAbraeumueberlapp % */
    int datacheck, blockwise, start_inside, abr_first, connect_lines, resort, abr_merge, param_norm, start_point, abr_round;
    int ntools; EgEstTool tool[10];
    char abr;               /* AbraeumVerf: 'Z' zeilebi, 'Y' zeileuni, 'C' radius, 'D' durchm, 'K' keine */
    double abr_angle;       /* Abraeumwinkel deg */
    char contours_on;       /* KonturenAuf: 'S' Spiegel (top), 'G' Grund (floor) */
    char engrave_type;      /* GravurTyp: 'V' vertieft, 'E' erhaben */
    int spant_on;           /* SpantZustellung */
    double spant_spiegel;   /* SpantSpiegelt */
    int abr_layers; char abr_split; double abr_ramp;   /* SpantAbrSchichtZ / SpantAbrAufteil / SpantAbrEintWi */
    double box[4];          /* Grenze_x0 / y0 / x1 / y1 (mm) */
} EgEstDat;

/* the engine's output, ENGTOOLP.LG1: per engine tool the motion records (mm; z = depth below the top, see
   eg_lg1_to_moves) */
typedef struct { int tool; char name[41]; EgRecs rec; } EgToolPath;
typedef struct { EgToolPath *tp; int n; } EgResult;

/* the link / height values engrave.dll uses when it turns the records into moves (FUN_180041910, FUN_18004aea0,
   FUN_18004abf0), part units */
typedef struct {
    double top, depth, fly;               /* P+0x190; cut depth below top (> 0); feed plane height above top */
    double feed_z, clear_z, retr_z;       /* P+0x198 / +0x1a0 / +0x1a8 */
    int clr_on, retr_on, clr_startend;    /* P+0x148 / +0x149 / +0x14a */
    int rapid_on; double rapid_z;         /* P+0x1b0 / +0x1b8 */
    int p180;                             /* P+0x180 = op +0x141 (record +0x1a9): link style, see eg_lg1_to_moves */
    double feed, plunge, retract;         /* T+0x78 / +0x80 / +0x88 */
    double f;                             /* engine scale: 25.4 for inch parts, 1 for mm */
} EgLink;

/* the op's inputs (engrave.dll) */
typedef struct {
    const uint8_t *S;       /* the 0x1d6-byte Engrave block (record +0x12f0; +0x12f8 in 10560-byte 2018 records) */
    int metric;             /* part units: 1 mm, 0 inch */
    int tool_type;          /* Mastercam tool type (12 chamfer mill, 21 engrave tool, ...) */
    double tool_dia, tool_angle, tool_tip_dia;   /* TL tool +8, +0x20 (deg per side), +0x3f8 */
    double feed, plunge, retract, rpm;           /* op record +0x378 / +0x380 / +0x388, tool rpm */
    int op141;              /* record +0x1a9 */
    double plane_z, chain_z;   /* active plane origin z, chain height (FUN_180019800) */
} EngraveIn;

/* chains: closed polylines in the op's plane (part units); pts[k] = x0 y0 x1 y1 ... */
typedef struct { const double *xy; int n; } EgChain;

int engrave_setup(const EngraveIn *in, EgEstDat *e, EgLink *lk, char *err, size_t errn);
int eg_engine(const EgEstDat *e, const EgRecs *contours_mm, EgResult *out, char *err, size_t errn);   /* 1 = ok */
int eg_lg1_to_moves(const EgResult *r, const EgLink *lk, Move **m, int *nm);
int engrave_generate(const EngraveIn *in, const EgChain *ch, int nch, Move **m, int *nm, char *err, size_t errn);
void eg_result_free(EgResult *r);
void eg_recs_free(EgRecs *r);
int eg_recs_add(EgRecs *r, int tag, double x, double y, double z);

#endif
