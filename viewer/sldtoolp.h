/* sldtoolp - a toolpath's solid faces, read-only (stage S3 of formats/surface_selection.md, section 7).
 *
 * A toolpath's solid reference (TP record kind 4, entity class 0x10000) carries a link id (+0x9a; 2018: +0xaa) naming an SldToolPOp
 * (ops-history type 0x18) in that solid's own history (the McOpsFile after the XT, formats/solid_history.md 2). The SldToolPOp lists
 * the picked faces - per face the id of an McEptr record (a face name of the history, formats/solid_history.md 3.2), the face's box
 * centre at pick time and the face's type (0 drive, 4 check, 0x20 gouge) - or says "the whole body" with one type for it.
 *
 * Sources (mcgeomsld.dll; dumps decomp/solid_history/, decomp/surfsel/, the S3 dumps quoted in formats/surface_selection.md 7):
 *   McSldEntOp::Read FUN_180274c80, SldToolPOp::Read FUN_18028e9d0 (+ size FUN_18028de60), the post-load id resolution
 *   (vtable slot 1 FUN_1802901b0 -> FUN_180275f30 mode 0 -> FUN_18026a240), the link query (slot 41 FUN_18028e490),
 *   sld_num_of_surfaces_in_toolp @1800c1820 (the counts), SldToolPOp::GetToolPSurfSelections FUN_18028e300 (the lists).
 *
 * NOT DECODED / not reproducible here (formats/surface_selection.md 7.4): which XT face a picked face is. Mastercam finds it only
 * after replaying the history with Parasolid (the faces' CNC/mcEptrPtr attributes; FUN_180269310 matches the record's name and
 * breaks ties by the stored box centre). Until the user decides (question SS-5) every picked face has xt_face = -1. Whole-body
 * references are all faces of the body (sld_num_of_surfaces_in_toolp). */
#ifndef SLDTOOLP_H
#define SLDTOOLP_H
#include <stdint.h>
#include "mcam_load.h"

typedef struct {
    uint64_t id;            /* McSldEntOp +0x98[i]: the McEptr record id stored for the face (its save-time address) */
    double pt[3];           /* +0xa0[i]: the face's box centre when it was picked (FUN_180275980 ps_get_bbox_center; part units,
                               FUN_18026e030), Mastercam's tie-breaker between faces of the same name */
    int type;               /* +0x150[i] (+0x14c entries): 0 drive, 4 check, 0x20 gouge (sld_set_toolp_srf_type); -1 = no entry */
    /* the McEptr record the id resolves to (FUN_18026a240: the ops from the last to the first, each op's first list then its second;
       the first record whose stored id equals it); rec_op -1 = unresolved (Mastercam then has a null record: no face) */
    int rec_op, rec_list, rec_idx;    /* index in Solid.ops, 1 = op+0x48 list / 2 = op+0x68 list, index in that list */
    uint32_t rec_kind, rec_opid;      /* McEptr +0 kind, +4 op id */
    uint64_t rec_ref1, rec_ref2;      /* +8, +0x10 */
    int xt_face;            /* the face's ordinal among the XT's FACE nodes (xt_faces()); -1 = NOT DECODED (SS-5) */
} SldTpFace;

typedef struct {
    int op;                 /* index in Solid.ops of the SldToolPOp */
    int op_id;              /* its op+0x34 */
    int kind;               /* +0xf0: 0 = surfaces (faces), 1 / 2 = chains (formats/mcam.md "Solid chains") */
    int link;               /* +0xf4: the link id (FUN_18028e490) */
    int whole;              /* +0x90: 1 = the whole body (FUN_180274c20) */
    int body_type;          /* +0x148: the whole body's type (0 drive, 4 check, 0x20 gouge) */
    int nfaces; SldTpFace *faces;     /* +0x94 picked faces (kind 0) */
    int nedges;             /* +0xa8 (chain kinds; the edge list is not resolved here) */
    int ntypes;             /* +0x14c: GetToolPSurfSelections asserts ntypes == nfaces for kind 0 (SldToolPOp.cpp l.0x6a1) */
    int body_at_op;         /* 1 = the solid's XT is the body this op sees: the history position is the op count and every op from
                               this one on is a toolpath op (mc_opType_is_toolpath: 0x18, 0x1b, 0x1c) [inferred: toolpath ops add no
                               geometry]; 0 = the body at the op needs a replay (NOT reproducible) */
} SldToolP;

typedef struct {            /* sld_num_of_surfaces_in_toolp(solid, link, &check, &gouge) for one solid and link */
    int nops;               /* SldToolPOps with that link and kind 0 (Mastercam sums over all of them; 1 in every corpus case) */
    int total, check, gouge;    /* total faces (whole bodies: the body's face count; picks: +0x94 entries, unresolved ones too),
                               check (type 4) and gouge (0x20) faces; drive = total - check - gouge (msurf_ent_count) */
    int known;              /* 0 = a whole body's face count is not known (XT not readable or body_at_op 0): total / check / gouge
                               leave it out */
} SldTpCount;

typedef struct {            /* one face of a reference as the later stages use it: the XT face and the role */
    int xt_face;            /* XT FACE ordinal (xt_faces()), -1 = NOT DECODED */
    int type;               /* 0 drive, 4 check, 0x20 gouge (whole body: +0x148; picks: +0x150[i]) */
} SldTpSel;

/* the solid of a TP record: by its SOLID_ID ent_idn (TP +0x6c) / by its SOLID_ID eptr (TP +0x80 = the ops header's u64 and op+0x18) */
const Solid *sldtoolp_solid_by_idn(const McamFile *f, int32_t idn);
const Solid *sldtoolp_solid_by_eptr(const McamFile *f, int64_t eptr);

/* the SldToolPOp of solid s with link id `link` and kind 0 (the first in history order, as sld_get_toolp_surf_tags via
   FUN_18026a5c0); 1 = found, 0 = none, -1 = the op does not parse (err, 128 bytes). sldtoolp_free releases out. */
int sldtoolp_get(const Solid *s, int link, SldToolP *out, char *err);
void sldtoolp_free(SldToolP *t);
/* op k of s read as an SldToolPOp whatever its link / kind: 1 = ok, 0 = not type 0x18, -1 = does not parse (err) */
int sldtoolp_at(const Solid *s, int k, SldToolP *out, char *err);
/* sld_num_of_surfaces_in_toolp's counts; returns c->nops */
int sldtoolp_count(const Solid *s, int link, SldTpCount *c);
/* the XT faces of solid s: count (-1 = XT not readable: no schema / parse error, reason in err) and, if node_idx, the FACE node
   indices (malloc'd, the caller frees) */
int sldtoolp_xt_faces(const Solid *s, int **node_idx, char *err);
/* link id -> the faces and types, for S2 / S4: a whole body = every XT face with the body type (needs body_at_op and a readable XT;
   else *n = 0 and the return is -1), picks = one entry per resolved face (GetToolPSurfSelections drops unresolved ones) with
   xt_face -1 (NOT DECODED, SS-5). Returns the number of entries, *out malloc'd; 0 = no such op, -1 = not available (err). */
int sldtoolp_faces(const McamFile *f, int32_t solid_idn, int link, SldTpSel **out, char *err);

#endif
