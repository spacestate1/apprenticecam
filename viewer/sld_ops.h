/* sld_ops - Mastercam solid history (McOpsFile) records: read / write of the op classes decoded so far, the ops block header,
 * the SOLID_ID record and the SOLIDXMT segment of a solid made here (formats/solid_history.md; mcgeomsld.dll, Ghidra
 * ~/mastercam-re/ghidra_sld, dumps decomp/solid_history/).
 *
 * The writer is the inverse of Mastercam's readers and follows its writers:
 *   McOpsFile         read FUN_18026b3c0, write FUN_18026dc50 (header: u8 +0x0c, u8 +0x0e, u32 +0x10, u32 +0x14, u64 +0x20, u32 count;
 *                     then per op u32 type, u32 size (vfunc slot 19), the op's bytes (slot 21))
 *   McOperation base  read FUN_1802649f0, write FUN_180266560, size FUN_180263a70
 *   McChainOp         read FUN_180271cc0 (the write is its inverse; proven by the byte-identity round trip below)
 *   SldExtrudeOp      read FUN_18027bbe0, write FUN_18027cb40 (types 0 Extrude Cut, 1 Extrude Boss, 2 Extrude)
 * Only ops version >= 0x19 in 64-bit files (MCFile +0x31 == 0) is handled: the layouts of every user part (0x1a) and of Mastercam 2017
 * (0x1c). New op classes slot in through SldOp.type + their own part (SldOp.cls); an op whose class has no reader / writer here keeps its
 * stored bytes (SldOp.raw) and cannot be created or edited.
 * Proof: tools/tests/sld_ops_test.c re-serialises every extrude op of the user's parts and of the corpus byte for byte. */
#ifndef SLD_OPS_H
#define SLD_OPS_H
#include <stddef.h>
#include <stdint.h>

/* McEptr record: in memory 0x30 bytes, serialised 38 (FUN_1802649f0 / FUN_180266560; ops version >= 0x18 has 64-bit refs, > 0xf the two bytes) */
typedef struct {
    uint64_t id;            /* mem +0x20: the record's own address at save time (FUN_18026c690); only unique + consistent with the refs */
    uint32_t kind;          /* mem +0x00 (solid_history.md 3.2) */
    uint32_t op;            /* mem +0x04: the owning op's id */
    uint64_t ref1, ref2;    /* mem +0x08 / +0x10 */
    uint32_t x28;           /* mem +0x28 (0 in all 10387 user records) */
    uint8_t b2c, b2d;       /* mem +0x2c / +0x2d (0 in all) */
} SldEptr;

enum { SLD_OP_EXTRUDE_CUT = 0, SLD_OP_EXTRUDE_BOSS = 1, SLD_OP_EXTRUDE = 2, SLD_OP_FILLET = 9 };   /* factory FUN_180267440; opmanres 27601 + type */
enum { SLD_CLS_RAW = 0, SLD_CLS_EXTRUDE = 1, SLD_CLS_FILLET = 2 };

typedef struct {
    int type;               /* op type (+0x30; also written before the size) */
    int cls;                /* SLD_CLS_*: which class part follows the base */
    int fver;               /* the ops file's version (McOpsFile +0x08 = SOLIDXMT +0x49, mc_read_opsfile): it decides the layout, not the
                               op's own version +0x3c (corpus: ops of version 8 .. 0x1a inside 0x1a files) */
    /* McOperation base (2.3) */
    int n1; SldEptr *e1;    /* op+0x44 / +0x48: names of the faces / edges this op made */
    int n2; SldEptr *e2;    /* op+0x60 / +0x68 */
    int n3; uint64_t *p3;   /* op+0x80 / +0x88 (NOT DECODED) */
    char name[256];         /* op+0x20, written as u16 strlen + 1 + the bytes + 0 */
    uint8_t f9, f10, f11, f12;
    uint32_t x10;
    uint64_t solid_eptr;    /* op+0x18: the SOLID_ID's eptr */
    uint32_t id;            /* op+0x34 */
    uint32_t ver;           /* op+0x3c: op version (0x1a X9, 0x1c 2017) */
    uint8_t x40, x41;       /* op+0x40 / +0x41 (NOT DECODED: 0 / 12 in all 303 user ops) */
    /* McChainOp (2.4) */
    uint8_t x90;
    int nch;
    int *cnt;               /* entities per chain */
    int32_t *xa0;           /* op+0xa0 per chain (NOT DECODED, 0 / 1) */
    uint64_t **ent;         /* per chain: the chained entities' eptrs, in chain order */
    uint8_t *xc0, *xa8;     /* per chain (ver > 5 / ver >= 0x14) */
    uint8_t **rev;          /* per chain per entity: 1 = traversed end -> start */
    /* SldExtrudeOp (2.5) */
    uint8_t P[0x70];        /* SLD_EXTRUDE_PARAMS, raw (formats/extrude.md sec. 0) */
    int ntrim; uint64_t *trim; uint8_t *trim_data;   /* trim-to faces: ntrim u64 + ntrim x 0x18 bytes (NOT DECODED, 0 in all) */
    double (*cdata)[12];    /* per chain SLD_EXT_CHAIN_DATA: direction (3), PLANE_sf (origin, normal, x axis) */
    /* McSldEntOp (read FUN_180274c80, size FUN_180274ad0; formats/solid_fillet.md 6.1): op+0x90, list A (faces: record id + bbox centre),
       list B (edges), op+0xc0 (+ nB bytes when set), op+0xd0 n x 0x48 and op+0xe0 n x 0x60 (kept raw; empty in every fillet op) */
    uint8_t ex90;
    int na; uint64_t *ida; double (*pta)[3];
    int nb; uint64_t *idb; double (*ptb)[3];
    uint8_t exc0; uint8_t *bfl;
    int n48; uint8_t *r48; int n60; uint8_t *r60;
    /* SldFilletOp (read FUN_180281170, write FUN_180281c70, size FUN_1802808c0): SLD_FILLET_PARAMS 0x18 bytes raw, then the variable-
       radius items: per item i32 k, k x 8 bytes (item +0x08), k x 8 bytes (item +0x30) */
    uint8_t FP[0x18];
    int nvr; int *vrk; uint64_t **vra, **vrb;
    /* SLD_CLS_RAW: the op's bytes after the type / size */
    uint8_t *raw; size_t nraw;
} SldOp;

/* McOpsFile header (2.1) */
typedef struct {
    uint8_t regen;          /* +0x0c "needs regen" (0 in all 807 corpus solids) */
    uint8_t expanded;       /* +0x0e the Solids manager row's expanded state (mcam_load.h Solid.expanded) */
    uint32_t solid_id;      /* +0x10 N */
    uint32_t pos;           /* +0x14 history position (number of active ops) */
    uint64_t solid_eptr;    /* +0x20 */
} SldOpsHdr;

/* read one op (body = the bytes after u32 type / u32 size); 64-bit file, ops version `ver` >= 0x19. An extrude op that does not consume
   its bytes exactly is an error (Mastercam's own size check, sld 259 / 225). Other types are kept raw (cls SLD_CLS_RAW). 1 = ok */
int sld_op_read(int type, const uint8_t *body, size_t n, int ver, SldOp *o, char *err, size_t errn);
/* the op's bytes (without type / size), malloc'd; *n = size. NULL on an unsupported layout */
uint8_t *sld_op_write(const SldOp *o, size_t *n);
void sld_op_free(SldOp *o);
/* the whole ops block: header, count, the ops (type, size, bytes); malloc'd */
uint8_t *sld_opsfile_write(const SldOpsHdr *h, const SldOp *ops, int nops, size_t *n);

/* SLD_EXTRUDE_PARAMS field access (formats/extrude.md sec. 0; offsets P+x) */
typedef struct {
    int type;               /* P+0x00 0 Create / 1 Cut / 2 Boss */
    uint64_t target;        /* P+0x08 target body (DB eptr in the file) */
    int method;             /* P+0x10 0 Distance / 1 Through all */
    double dist;            /* P+0x18 */
    double vec[3];          /* P+0x20 */
    int trim, reverse, both, split;   /* P+0x38 .. +0x3b */
    int draft;              /* P+0x40 */
    int draft_rev;          /* P+0x44 */
    double draft_ang;       /* P+0x48 radians */
    int thin;               /* P+0x50 */
    int thin_dir;           /* P+0x54 */
    double thick, thick2;   /* P+0x58 / +0x60 */
    int draft_ends;         /* P+0x68 */
} SldExtP;
void sld_extp_get(const uint8_t *P, SldExtP *p);
/* SLD_FILLET_PARAMS (formats/solid_fillet.md 0): P+0 type (0 constant), +8 radius, +0x10 overflow, +0x14 mitered, +0x15 propagate */
typedef struct { int type; double radius; int overflow, mitered, propagate; } SldFilP;
void sld_filp_get(const uint8_t *P, SldFilP *p);
void sld_filp_put(uint8_t *P, const SldFilP *p);   /* the padding bytes (P+4, +0x16, +0x17) are left as they are */
/* writes the fields into P; bytes of P that are no field (padding) are left as they are (the caller zeroes P for a new op) */
void sld_extp_put(uint8_t *P, const SldExtP *p);


/* a solid made here, to be written into the part (mcam_saveops.c; formats/solid_history.md 5): the body (its XT is written at the save in
   the file's Parasolid version with the key sld_N), the attributes of its SOLID_ID record and its history. In ops: a chain entity's eptr
   and a McEptr record's entity ref are MCAM_EPTR_TOKEN(F.ents index) (mcam_saveops.h; resolved by the save once the entity has its eptr),
   record ids are unique per solid; the save sets each op's fver / ver / solid_eptr and a Cut / Boss op's P target (= the solid's eptr). */
struct OcsBody;
typedef struct SldNew {
    struct OcsBody *body;
    int level, color;
    char desc[256];                     /* the name after the solid id (sld_set_description) */
    uint8_t expanded;                   /* McOpsFile +0x0e: the Solids manager row's expanded state */
    int nops; SldOp *ops;
    char why[300];                      /* non-empty: this solid cannot be written (the save leaves it out and says why) */
} SldNew;
void sld_new_free(SldNew *s, int n);
#endif
