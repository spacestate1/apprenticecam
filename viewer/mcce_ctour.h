/* mcce_ctour.h - port of mcce.dll's MCCECalcContourOffset (formats/mcce_ctour_offset.md). PARTIAL: what it does not do is refused with a reason
 * (see the .md "NOT DECODED / NOT PORTED"). */
#ifndef MCCE_CTOUR_H
#define MCCE_CTOUR_H

typedef struct {
    char id;             /* 'L' / 'A' (pock_chn +0x08) */
    unsigned char ce;    /* CeFlags +0x12 (work value) */
    double a[5];         /* L: x1 y1 x2 y2; A: cx cy r sa sw (+0x18..+0x38) */
    double box[4];       /* SetChnMinMax: x0 y0 x1 y1 (+0x40..+0x58) */
    int src_sh, src_ent; /* the input entity this one comes from (+0x60 / +0x70 links); -1 = corner / link entity */
} McceEnt;

typedef struct {
    McceEnt *e; int n;
    int dir;             /* +0x50: 1 CCW, -1 CW, 0 open (input: as the bound's ccw byte, only 1 counts: CopyPockBndsToShAr; the closedness test decides an open chain) */
    int isl;             /* +0x54 bit 0 */
} McceShape;

typedef struct {
    McceShape *s; int n;
    const char *refused; /* not NULL: this input needs a path not ported (the message says which); s = NULL */
} McceResult;

/* MCCECalcContourOffset(1, d, in, out, flags, ang, gap, t1, t2) (decomp/mcce_all.c l.8571-8807): d > 0 left of travel, d < 0 right. Returns the
   shapes (malloc; mcce_shapes_free) with the entity's source (input shape / entity) links. */
McceResult mcce_contour_offset(const McceShape *in, int nin, double d, unsigned flags, double ang_deg, double gap, double t1, double t2);
/* the same on a CVe<CSh*> list made in the engine: each shape's dir is kept (0 = open), as AppendByCopy in the export */
McceResult mcce_contour_offset_sh(const McceShape *in, int nin, double d, unsigned flags, double ang_deg, double gap, double t1, double t2);
void mcce_shapes_free(McceResult *r);
/* PtInSh (MCCEPtInBnd flags 6): 1 on the boundary, 2 inside, 4 outside (decomp/mcce_all.c l.33-170, 13851-13888) */
int mcce_pt_in_shape(double x, double y, const McceShape *s, double tol);
/* PtInSh with the caller's stored entity boxes (McceEnt.box) and shape box (CSh +0x30), as the DLL reads them; e[k].ce is work space */
int mcce_pt_in_shape_ents(double x, double y, McceEnt *e, int n, const double box[4], double tol);

/* mcce.dll's region boolean (mcce_sobool.inc, formats/mcce_port_plan.md §9 M3). Lists in mcce's group order [islands..., outer]
   (isl bit 0 = island); results with isl = the nesting level (DetNestLvlShAr). refused != NULL: a path not ported. */
enum { MCCE_RB_MINUS = 0, MCCE_RB_ISECT = 1, MCCE_RB_UNION = 2 };
McceResult mcce_region_bool(int op, double tol, const McceShape *x, int nx, const McceShape *y, int ny, void *dump_file);   /* FUN_180153b30 / FUN_18015b2e0 / SoPockUnionAr (x only) */
int mcce_region_bool_file(const char *in, const char *out);   /* the unit oracle's port side (cam_cli sobool IN OUT; tools/peloader/mcce_sobool.c format) */
/* mcce.dll's MCCECalcOffset(n, d, in, out, flags, ang, t1, t2) (mcce_offset.inc, formats/mcce_port_plan.md §9 "M2 offset"): the region
   offset of the shapes (isl = the nesting level +0x54, dir +0x50 as the caller has them); d < 0 shrinks, d > 0 grows. Results with
   isl = the nesting level and the source links (src_sh / src_ent). refused != NULL: a path not ported. dump: a FILE * for the stages
   (the unit oracle) or NULL. */
McceResult mcce_offset(const McceShape *in, int nin, int npass, double d, unsigned flags, double ang_deg, double t1, double t2, void *dump);
int mcce_offset_file(const char *in, const char *out);   /* the unit oracle's port side (cam_cli sooff IN OUT; tools/peloader/mcce_off.c format) */
int mcce_nest_only(McceShape *s, int n, int *map);   /* DetNestLvlShAr alone: reordered, isl = the level, dir kept */
int mcce_dir_nest(McceShape *s, int n, int *map);   /* UpdDirShAr(0.001) + DetNestLvlShAr: s (and map) reordered, dir from the area, isl = the level */

#endif
