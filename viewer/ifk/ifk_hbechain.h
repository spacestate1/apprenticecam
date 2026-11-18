/* ifk_hbechain.h - HBE records -> Mastercam curves and chains: mcmsurf.dll FUN_180232540 (the last step of
 * mucbMakeIffSiluetBndy, Wireframe Turn Profile / Silhouette Boundary), with mucbMakeMcSpline, mucbMakeMcArc3D and
 * mucbAddCurveToChain. formats/interfacial.md 14.2. File-level port, no app wiring (14.6 lists what the wiring needs). */
#ifndef IFK_HBECHAIN_H
#define IFK_HBECHAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the 0x68-byte HBE record as the file holds it (FUN_18021f470 / FUN_18021f080 / FUN_180232540) */
typedef struct {
    double p0[3], p1[3];   /* +0x00 start, +0x18 end */
    double c[3];           /* +0x30 arc centre */
    double n[3];           /* +0x48 arc unit normal (FUN_180232540 reads n[2] = +0x58) */
    uint8_t type;          /* +0x60 1 line, 2 arc (others: skipped) */
    uint8_t first;         /* +0x61 first record of a curve */
    uint8_t pad[6];
} IfkHbe68;

/* the entities FUN_180232540 stores (store_ent with main_color / main_level / main_attrib, see 14.2.5) */
enum { IFK_ENT_LINE = 2, IFK_ENT_ARC = 4, IFK_ENT_SPLINE = 8 };
typedef struct {
    int type;
    double p0[3], p1[3];         /* ent +0x68 / +0x80: the end points chaining uses (curve_endpoints, FUN_1801e61d0) */
    /* arc (a_3d at ent +0x68, arc_2d_to_3d): centre (+0x30 x, y; +0x40 depth), r (+0x48), start angle (+0x50), sweep (+0x58), view (+0x60) */
    double c[3], r, sa, sw;
    short view;
    /* spline (make_spline_from_pts): the nodes, closed flag, end tangents (code 2 at both ends; unused when closed) and the
       coefficients of make_spline_coefs_open / _closed: nseg = nnode - 1 segments of 12 doubles [power][xyz] */
    int nnode, closed;
    double (*node)[3];
    double t0[3], t1[3];
    double *coef;
} IfkCurve;

typedef struct { int ent, ep; } IfkChainItem;      /* CHAIN_ENT: +8 the entity, +0x18 the end it is entered at (0 start, 1 end) */
typedef struct { IfkChainItem *it; int n, cap, closed; } IfkChain;

typedef struct {
    IfkCurve *ents; int nents, capents;   /* every entity stored, in store_ent order */
    IfkChain *chains; int nchains;        /* *param_6: the closed chains in order, or the one open chain when none closed */
    int made;                             /* cVar2 at the end: the result is returned only when set */
    int nopen_dropped;                    /* entities of an open chain left when closed chains exist (stored, not returned) */
    int undefined_hit;                    /* a path the original runs on stale memory (14.2.3): result not defined */
} IfkHbeChains;

typedef struct {
    int flat;            /* param_2 = (spin == 0 || !dat[0x55]): every z set to depth */
    double depth;        /* param_5: FUN_18021ef60(dat) when dat[0x4f], else mucbConstrDepthWcsOrigin() */
    double maxang;       /* param_7 = dat+8 (radians; < 0: every line record is a curve of its own) */
    double tol;          /* mccore ?tol@@3NA (duplicate points, constr_arc_new, make_spline_from_pts closed test) */
    double nc_tol;       /* mccore ?nc_tol@@3NA (mucbAddCurveToChain, set_chain_closed) */
    double chord_dev;    /* Mastercam.dll ?curve_chord_dev@@3NA (FUN_1802323a0's densification) */
    int max_spline_pts;  /* CSplineNodeMgr::get_max_n_spline_pts (4000 unless grown) */
    int chain_ignore_depths;   /* mccore ?chain_ignore_depths@@3DA (set_chain_closed's distance FUN_1801dddb0) */
    short view;          /* GetActiveViewNo(0) */
    double vm[9];        /* rows of that view's matrix (mcViews[view] +8); view_to_world = R^T v (view < 2: the identity, untouched) */
} IfkHbeChainParams;

/* FUN_180232540(file, flat, 0, 1.0, depth, &out, maxang, 0) on the records in file order. Returns 0, -1 on memory. */
int ifk_hbe_to_chains(const IfkHbe68 *rec, int n, const IfkHbeChainParams *p, IfkHbeChains *out);
void ifk_hbe_chains_free(IfkHbeChains *out);
/* read a whole HBE file (0x68-byte records until EOF); returns the count, -1 on error */
int ifk_hbe_read(const char *path, IfkHbe68 **rec);

#ifdef __cplusplus
}
#endif
#endif
