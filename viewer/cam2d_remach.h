/* cam2d_remach.h - port of the mccore pock_bound / pbmap functions Contour type 5 "Remachining" needs (formats/contour_remachining.md sections 3-4;
 * decomp citations in cam2d_remach.c). Each entity keeps the 0x88-byte pock_chn fields the port uses: id, a[5] = line x0 y0 x1 y1 / arc cx cy r sa sw,
 * box = bbox_gt +0x40..+0x58, h = +0x78. */
#ifndef CAM2D_REMACH_H
#define CAM2D_REMACH_H

#include "mcce_ctour.h"

typedef struct { int rec; char id; double a[5], box[4]; double h; } RmEnt;
typedef struct RmB {
    unsigned char valid, broken, isl, ccw, done;   /* +0 .. +4 */
    unsigned char tr, f45;                         /* +0x44 (FUN_1805dec50: a piece tracing another bound), +0x45 (propagated by merge) */
    int gid;                                       /* +8 */
    int n; RmEnt *e;                               /* +0xc, +0x10 */
    double bbox[4];                                /* +0x20..+0x38 */
    double len;                                    /* +0x48 */
    struct RmB *par, *prev, *next;                 /* +0x50, +0x58, +0x60 */
    int nlead_in, nlead_out;                       /* ours (Pocket Remachining): the lead entities kept at the start / end of a piece (their flags: FUN_1801bdc00 entity +0 bit 0 / 1 clear) */
} RmB;

typedef struct { char flag, used; int ring; double x, y; int idx, row; void *prev, *next; } RmPt;   /* 0x30 bytes in the DLL */
typedef struct { int n, cap; RmPt *p; } RmRow;
typedef struct { double cell, y0; int nrows; RmRow **rows; } RmMap;
typedef struct RmBnd { char on; RmPt *first; struct RmBnd *next; } RmBnd;      /* pbmap_bnd: {1, first record, next} */
typedef struct RmRem { unsigned char active; double start[2], end[2]; short idx; RmB *b; struct RmRem *next; } RmRem;   /* remachine_bound (0x40) */

extern double rm_tol, rm_nc_tol, rm_chain_tol;   /* mccore tol / nc_tol / chain_tol */

/* pbmap */
int  rm_map_init(RmMap *m, double ymin, double ymax, double cell);
void rm_map_free(RmMap *m);
int  rm_map_copy(RmMap *dst, const RmMap *src);
int  rm_map_diff(RmMap *a, const RmMap *b, double tol);
double rm_map_area(const RmMap *m);
void rm_verify_line(const double l[4], double r, RmMap *m);
void rm_verify_arc(const double a[5], double r, RmMap *m);
void rm_fill_bounds(const RmB *l, RmMap *m);                       /* pbmap_fill_pock_bounds */
void rm_verify_bounds(RmB *b, double r, RmMap *m);                 /* verify_pock_bounds */
int  rm_map_trace(RmMap *m, double dtol, int inv, RmBnd **out);    /* pbmap_to_pbmap_bnds */
void rm_bnds_free(RmBnd *b);
void rm_verify_bnds(RmBnd *b, double r, RmMap *src, RmMap *dst);   /* verify_pbmap_bounds */
void rm_filter_bnds(RmMap *m, RmBnd *b, double tol);               /* filter_pbmap_bnds */
int  rm_bnds_to_bounds(RmMap *m, RmBnd *b, RmB **out);             /* pbmap_bnds_to_pock_bounds */

/* pock_bound lists (next / prev links) */
RmB *rm_b_new(int n);
RmB *rm_b_copy(const RmB *b);
void rm_b_free(RmB *b);
void rm_b_free_list(RmB **b);
void rm_b_append(RmB **head, RmB *b);
void rm_b_info(RmB *b);          /* build_pock_bound_info */
void rm_b_infos(RmB *b);         /* build_pock_bounds_info */
void rm_b_bbox(RmB *b);          /* pock_bound_bbox */
void rm_b_bboxs(RmB *b);         /* pock_bounds_bbox */
void rm_sort(RmB **head, int p3_overlap, double tol, short *ngroups);   /* sort_pock_bounds(NULL, 0, p3, tol, &head, &n) */
int  rm_trim(RmB **passes, RmB *rest, RmB **out);                       /* pock_trim_toolpath */
int  rm_merge_pock(RmB **head, int facing);                            /* merge_pock_bounds (facing 0) / merge_pock_bounds_facing_islands (1): FUN_1805e02e0 */
void rm_b_reverse(RmB *b, int keep);                                   /* reverse_pock_bound */
int  rm_bound_intersect(const RmB *a, const RmB *b);                   /* pock_bound_intersect */
int  rm_merge(RmB **head);                                              /* remach_merge_adj_segs */
int  rm_rem_add(RmB *piece, short idx, RmRem **list);                   /* add_remachine_bound */
int  rm_group(char *cont, double cur[2], RmRem *list, RmRem **group);   /* find_remachine_group */
void rm_rem_free(RmRem **list, int bounds);                            /* free_remachine_bounds */
int  rm_pt_in_ctour(const RmB *b, double x, double y, double nc, double t);   /* pt_in_ctour */
int  rm_contour_nested(const RmB *a, const RmB *b, double t);   /* IsContourNested (mccore pock l.2270): a inside b, tolerance t */
void rm_gt_ends(const RmEnt *e, double s[2], double en[2]);

/* merge_pock_bounds FUN_1805e02e0 param_2 = 0 (Pocket Remachining l.338304, l.338678), pock_bound_intersect, is_ctour_nested, disjoint_pock_bound_bbox, reverse_pock_bound(b, 0), delete_pock_bound */
int  rm_merge_bounds(RmB **list);
int  rm_is_ctour_nested(const RmB *a, const RmB *b);
int  rm_disjoint(const RmB *a, const RmB *b);
void rm_b_del(RmB **head, RmB *b);

/* pbmap tool / toolpath (Pocket Constant Overlap Spiral, mcmill FUN_1801e8650; decomp/pocket_b6/decomp_tspiral_pbmap.c) */
typedef struct { int n, nm1, nw, step; double area, area_step, *w, *dw, *w2; } RmTool;   /* pbmap_tool */
typedef struct RmTp { double x, y, bx, by; struct RmTp *prev, *next; } RmTp;              /* pbmap_tp */
int  rm_tool_init(double r, double cell, RmTool *t);                                     /* pbmap_init_tool */
void rm_tool_free(RmTool *t);                                                            /* pbmap_free_tool */
int  rm_bnd_to_tp(RmMap *cur, RmMap *base, RmTool *t, RmBnd *b, int inv, RmTp **out, double r); /* pbmap_bnd_to_tp */
void rm_filter_tp(RmTp **h, double t);                                                   /* filter_pbmap_tp */
void rm_tp_free(RmTp *t);
void rm_reverse_bnds(RmBnd *b);                                                          /* reverse_pbmap_bnds */
void rm_map_set_used(RmMap *m, char v);                                                  /* set_pbmap_used */

/* FUN_180050de0 (decomp/mcmill_all.c l.55322-55835, "add_remach_entry_ents") + the lead geometry FUN_1801c2b30 (l.311918): line / arc / sweep / ramp values of one side,
   record offsets 0x900.. / 0x948.. of Lead In/Out */
typedef struct { double r_in, sw_in, len_in, dz_in_arc, dz_in_line, r_out, sw_out, len_out, dz_out_arc, dz_out_line; int perp_in, perp_out; } RmLeads;
/* gouge = op +0x929 (record 0x991): FUN_1801c46f0 on each side against chain and the gouge bound ref (may be NULL) */
RmB *rm_add_leads(const RmB *piece, const RmLeads *l, char side, RmB *chain, RmB *ref, int gouge, int *n_entry, int *n_exit, int *t16, int *t17);
/* FUN_1801c46f0 (mcmill 0x1801c46f0) on one side's lead entities (entry: [line,] arc; exit: arc[, line]); *pn and ents updated (tools/tests/remach_oracle.sh LGOUGE) */
void rm_lead_gouge_side(RmB *chain, RmB *ref, RmEnt *ents, int *pn, char side, int entry);
/* FUN_1801c2b30 (mcmill 0x1801c2b30): one lead element on the end of e: ln 0 arc / 1 line, ex 0 entry / 1 exit (tools/tests/remach_oracle.sh LEADGT) */
RmEnt rm_lead_gt(const RmEnt *e, int ex, int ln, char side, double len, double rad, double sw, int perp);

/* ccomp_pock (formats/contour_remachining.md section 3): MCCECalcContourOffset through the port; *why = reason when it refuses */
int  rm_ccomp_pock(const RmB *bound, RmB **out, double dist, char side, double angle_arg, int p6_oneway, int p7, int p8, const char **why);

#endif
