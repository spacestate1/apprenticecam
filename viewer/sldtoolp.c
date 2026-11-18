/* sldtoolp.c - a toolpath's solid faces, read-only (sldtoolp.h; formats/surface_selection.md 7). Every step cites mcgeomsld.dll. */
#include "platform.h"
#include "sldtoolp.h"
#include "xt_mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t r64(const uint8_t *p) { return (uint64_t)r32(p) | (uint64_t)r32(p + 4) << 32; }

const Solid *sldtoolp_solid_by_idn(const McamFile *f, int32_t idn) {
    for (int i = 0; idn && i < f->nsolids; i++) if (f->solids[i].idn == idn) return &f->solids[i];
    return NULL;
}
/* the ops header's u64 at +10 is McOpsFile +0x20 = the SOLID_ID eptr (FUN_18026b3c0; formats/solid_history.md 2.1) */
const Solid *sldtoolp_solid_by_eptr(const McamFile *f, int64_t eptr) {
    for (int i = 0; eptr && i < f->nsolids; i++) {
        const Solid *s = &f->solids[i];
        if (s->hist_ok && s->ops_n >= 18 && (int64_t)r64(s->ops_raw + 10) == eptr) return s;
    }
    return NULL;
}

/* McEptr records of op k (McOperation::Read FUN_1802649f0): i32 n1 + n1 x 38 bytes, i32 n2 + n2 x 38 (ops version >= 0x18:
   u64 id, u32 kind, u32 op id, u64 ref1, u64 ref2, u32, u8, u8). Returns the record start of list `list` (1 / 2), *n = its count. */
static const uint8_t *op_records(const Solid *s, int k, int list, int *n) {
    const SolidOp *o = &s->ops[k]; const uint8_t *q = s->ops_raw + o->off;
    int32_t n1 = (int32_t)r32(q);
    if (list == 1) { *n = n1 > 0 ? n1 : 0; return q + 4; }
    q += 4 + (n1 > 0 ? (size_t)n1 * 38 : 0);
    int32_t n2 = (int32_t)r32(q); *n = n2 > 0 ? n2 : 0; return q + 4;
}

/* FUN_18026a240(opsfile, id, NULL, op id, 0) as the post-load fix-up calls it (FUN_1802901b0 -> FUN_180275f30 mode 0): ops
   version >= 6, so the old-version table (opsfile +0x80) is not searched; the ops from the last to the first, each op's list
   +0x48 then +0x68, the first record whose stored id (+0x20, read back by FUN_1802649f0) equals `id`. id 0 -> none. */
static void resolve(const Solid *s, SldTpFace *fc) {
    fc->rec_op = fc->rec_list = fc->rec_idx = -1;
    if (!fc->id) return;
    for (int k = s->nops - 1; k >= 0; k--)
        for (int l = 1; l <= 2; l++) {
            int n; const uint8_t *r = op_records(s, k, l, &n);
            for (int i = 0; i < n; i++, r += 38)
                if (r64(r) == fc->id) {
                    fc->rec_op = k; fc->rec_list = l; fc->rec_idx = i;
                    fc->rec_kind = r32(r + 8); fc->rec_opid = r32(r + 12); fc->rec_ref1 = r64(r + 16); fc->rec_ref2 = r64(r + 24);
                    return;
                }
        }
}

typedef struct { const uint8_t *q, *e; int bad; } Rd;
static int rd_ok(Rd *r, size_t n) { if (r->bad || (size_t)(r->e - r->q) < n) { r->bad = 1; return 0; } return 1; }
static uint8_t g8(Rd *r) { if (!rd_ok(r, 1)) return 0; return *r->q++; }
static int32_t g32(Rd *r) { if (!rd_ok(r, 4)) return 0; int32_t v = (int32_t)r32(r->q); r->q += 4; return v; }
static uint16_t g16(Rd *r) { if (!rd_ok(r, 2)) return 0; uint16_t v = r16(r->q); r->q += 2; return v; }
static const uint8_t *gskip(Rd *r, size_t n) { if (!rd_ok(r, n)) return NULL; const uint8_t *p = r->q; r->q += n; return p; }
static size_t cnt(int32_t n, size_t each) { return n > 0 ? (size_t)n * each : 0; }

/* op k of s as an SldToolPOp. Read order (ops version V = the SOLIDXMT +0x49 value, mc_read_opsfile; 64-bit file):
   McSldEntOp::Read FUN_180274c80 after the base: u8 +0x90; i32 n +0x94, n x u64 +0x98, n x 0x18 +0xa0; i32 n2 +0xa8, n2 x u64
   +0xb0, n2 x 0x18 +0xb8; u8 +0xc0 (+ n2 bytes +0xc8 when set and n2 > 0); V > 2: i32 n3 +0xd0, n3 x 0x48; V > 3: i32 n4 +0xe0,
   n4 x 0x60 (the 0x3a / 0x51 packed forms are for file versions 1..9 only).
   SldToolPOp::Read FUN_18028e9d0: 8 bytes +0xf0 (i32 kind) / +0xf4 (i32 link); V > 2: i32 + bytes (+0xf8), i32 + bytes (+0x108),
   i32 n +0x118 + n x i16 + n x (u16 len + len bytes); V >= 0x1a: i32 n +0x130 + n x i32 + n x (u16 len + bytes) (else +0x130 = the
   +0x108 count, nothing read); V >= 5: u16 +0x148, i32 n +0x14c + n x i16 +0x150 (else +0x148 = 0 and, for kind 0, +0x150 = +0x94
   zeros); V > 0xb: i32 n +0x158, per entry u32 flag, flag != 0 -> an object FUN_180278610 / FUN_180278d30 (NOT DECODED: refused).
   The reader then checks the bytes read against FUN_18028de60's size (message 0x103 on a mismatch): so must we. */
static int parse_tp(const Solid *s, int k, SldToolP *t, char *err) {
    const SolidOp *o = &s->ops[k]; int V = s->ver;
    memset(t, 0, sizeof *t); t->op = k; t->op_id = o->id;
    Rd r = { s->ops_raw + o->off + o->base_n, s->ops_raw + o->off + o->size, 0 };
    t->whole = g8(&r);
    int32_t n = g32(&r); const uint8_t *ids = gskip(&r, cnt(n, 8)), *pts = gskip(&r, cnt(n, 0x18));
    int32_t n2 = g32(&r); gskip(&r, cnt(n2, 8)); gskip(&r, cnt(n2, 0x18));
    uint8_t c0 = g8(&r); if (n2 > 0 && c0) gskip(&r, (size_t)n2);
    if (V > 2) { int32_t n3 = g32(&r); gskip(&r, cnt(n3, 0x48)); }
    if (V > 3) { int32_t n4 = g32(&r); gskip(&r, cnt(n4, 0x60)); }
    t->kind = g32(&r); t->link = g32(&r);
    if (V > 2) {
        int32_t a = g32(&r); gskip(&r, cnt(a, 1));
        a = g32(&r); gskip(&r, cnt(a, 1));
        a = g32(&r); gskip(&r, cnt(a, 2));
        for (int i = 0; i < a && !r.bad; i++) gskip(&r, g16(&r));
    }
    if (V >= 0x1a) { int32_t a = g32(&r); gskip(&r, cnt(a, 4)); for (int i = 0; i < a && !r.bad; i++) gskip(&r, g16(&r)); }
    const uint8_t *types = NULL;
    if (V >= 5) { t->body_type = g16(&r); t->ntypes = g32(&r); types = gskip(&r, cnt(t->ntypes, 2)); if (t->ntypes < 0) t->ntypes = 0; }
    else { t->body_type = 0; t->ntypes = (n > 0 && t->kind == 0) ? n : 0; }
    if (V > 0xb) {
        int32_t a = g32(&r);
        for (int i = 0; i < a && !r.bad; i++)
            if (g32(&r) != 0) { snprintf(err, 128, "op %d: SldToolPOp sub-object (FUN_180278610) NOT DECODED", k); return -1; }
    }
    if (r.bad || r.q != r.e) { snprintf(err, 128, "op %d: SldToolPOp body does not parse to its size (%zu of %zu bytes)", k,
                                        (size_t)(r.q - (s->ops_raw + o->off)), o->size); return -1; }
    t->nedges = n2 > 0 ? n2 : 0;
    t->nfaces = n > 0 ? n : 0;
    t->faces = xcalloc(t->nfaces ? (size_t)t->nfaces : 1, sizeof *t->faces);
    for (int i = 0; i < t->nfaces; i++) {
        SldTpFace *fc = &t->faces[i];
        fc->id = r64(ids + 8 * (size_t)i);
        for (int j = 0; j < 3; j++) { uint64_t u = r64(pts + 0x18 * (size_t)i + 8 * (size_t)j); memcpy(&fc->pt[j], &u, 8); }
        fc->type = i < t->ntypes ? (types ? (int16_t)r16(types + 2 * (size_t)i) : 0) : -1;
        fc->xt_face = -1;                             /* NOT DECODED (sldtoolp.h, SS-5) */
        resolve(s, fc);
    }
    /* the body this op sees: FUN_18026c170 rolls the solid back to the op; the stored XT is the body at the history position
       (header +0x14). Equal when the position is the op count and no op from this one on adds geometry [inferred: toolpath ops
       (mc_opType_is_toolpath 0x18 / 0x1b / 0x1c) are the ones FUN_18026a110 leaves out of old files; their regen only maps tags] */
    t->body_at_op = s->pos == s->nops;
    for (int j = k; j < s->nops; j++) if (s->ops[j].type != 0x18 && s->ops[j].type != 0x1b && s->ops[j].type != 0x1c) t->body_at_op = 0;
    return 1;
}

void sldtoolp_free(SldToolP *t) { free(t->faces); memset(t, 0, sizeof *t); }

/* sld_get_toolp_surf_tags: FUN_18026a5c0 (the op index for a link) then the op's slot-41 query must give kind 0 and that link */
int sldtoolp_at(const Solid *s, int k, SldToolP *out, char *err) {
    char e[128] = "";
    memset(out, 0, sizeof *out);
    if (!s || !s->hist_ok || s->ver < 0x18) { if (err) snprintf(err, 128, "no parsed history (ops version 0x18 or later)"); return -1; }
    if (k < 0 || k >= s->nops || s->ops[k].type != 0x18) return 0;
    int rc = parse_tp(s, k, out, e);
    if (rc < 0 && err) snprintf(err, 128, "%s", e);
    return rc;
}

int sldtoolp_get(const Solid *s, int link, SldToolP *out, char *err) {
    char e[128] = "";
    memset(out, 0, sizeof *out);
    if (!s || !s->hist_ok || s->ver < 0x18) { if (err) snprintf(err, 128, "no parsed history (ops version 0x18 or later)"); return -1; }
    for (int k = 0; k < s->nops; k++) {
        if (s->ops[k].type != 0x18) continue;   /* slot 41 answers for SldToolPOp (and the FBM drill / lathe classes 0x1b / 0x1c, not here) */
        SldToolP t; int rc = parse_tp(s, k, &t, e);
        if (rc < 0) { if (err) snprintf(err, 128, "%s", e); return -1; }
        if (t.link == link && t.kind == 0) { *out = t; return 1; }
        sldtoolp_free(&t);
    }
    return 0;
}

int sldtoolp_xt_faces(const Solid *s, int **node_idx, char *err) {
    if (node_idx) *node_idx = NULL;
    if (!s->xt || !s->xt_size) { if (err) snprintf(err, 128, "no XT"); return -1; }
    XtSchema *sc = xt_schema_for(s->xt, s->xt_size);
    if (!sc) { if (err) snprintf(err, 128, "no Parasolid schema for this XT"); return -1; }
    return xt_faces(sc, s->xt, s->xt_size, node_idx, err);
}

/* sld_num_of_surfaces_in_toolp @1800c1820 (decomp/surfsel/mcgeomsld_surfsel.c 426-525): for every op of the history whose slot-41
   query gives (kind 0, link): roll back to it; whole (slot 16 = +0x90) -> total += the body's face count (PK_BODY_ask_faces), and
   check / gouge += it when +0x148 is 4 / 0x20; then total += the face list's size (+0x94, FUN_180274240: unresolved entries too),
   check / gouge += the entries of the type list (+0x150) that are 4 / 0x20. */
int sldtoolp_count(const Solid *s, int link, SldTpCount *c) {
    memset(c, 0, sizeof *c); c->known = 1;
    if (!s || !s->hist_ok || s->ver < 0x18) return 0;
    int nxt = -2;
    for (int k = 0; k < s->nops; k++) {
        if (s->ops[k].type != 0x18) continue;
        SldToolP t; char e[128];
        if (parse_tp(s, k, &t, e) < 0) { c->known = 0; continue; }
        if (t.link == link && t.kind == 0) {
            c->nops++;
            if (t.whole) {
                if (nxt == -2) nxt = sldtoolp_xt_faces(s, NULL, NULL);
                if (nxt < 0 || !t.body_at_op) c->known = 0;
                else { c->total += nxt; if (t.body_type == 4) c->check += nxt; else if (t.body_type == 0x20) c->gouge += nxt; }
            }
            c->total += t.nfaces;
            for (int i = 0; i < t.ntypes && i < t.nfaces; i++) { if (t.faces[i].type == 4) c->check++; else if (t.faces[i].type == 0x20) c->gouge++; }
        }
        sldtoolp_free(&t);
    }
    return c->nops;
}

/* SldToolPOp::GetToolPSurfSelections FUN_18028e300 (kind 0): the face tags (FUN_180274280: each id matched against the faces'
   mcEptrPtr records) with their types (FUN_18028dc30), entries whose tag is 0 removed; whole (+0x90) -> the body tag with the type
   +0x148 in the body lists. Here a body is expanded to its faces as sld_num_of_surfaces_in_toolp counts it. */
int sldtoolp_faces(const McamFile *f, int32_t solid_idn, int link, SldTpSel **out, char *err) {
    *out = NULL;
    const Solid *s = sldtoolp_solid_by_idn(f, solid_idn);
    if (!s) { if (err) snprintf(err, 128, "no solid with idn %d", solid_idn); return -1; }
    SldToolP t; int rc = sldtoolp_get(s, link, &t, err);
    if (rc <= 0) { if (rc == 0 && err) snprintf(err, 128, "no SldToolPOp with link %d", link); return rc; }
    int n = 0, cap = 0; SldTpSel *v = NULL;
    for (int i = 0; i < t.nfaces; i++) {
        if (t.faces[i].rec_op < 0) continue;     /* FUN_18028e300 drops faces whose tag is 0 */
        if (n == cap) { cap = cap ? cap * 2 : 16; v = xrealloc(v, sizeof *v * (size_t)cap); }
        v[n].xt_face = t.faces[i].xt_face; v[n].type = t.faces[i].type < 0 ? 0 : t.faces[i].type; n++;
    }
    if (t.whole) {
        int nx = t.body_at_op ? sldtoolp_xt_faces(s, NULL, err) : -1;
        if (nx < 0) { if (err && !t.body_at_op) snprintf(err, 128, "the body at op %d needs a history replay (NOT reproducible)", t.op);
                      free(v); sldtoolp_free(&t); return -1; }
        for (int i = 0; i < nx; i++) {
            if (n == cap) { cap = cap ? cap * 2 : 16; v = xrealloc(v, sizeof *v * (size_t)cap); }
            v[n].xt_face = i; v[n].type = t.body_type; n++;
        }
    }
    sldtoolp_free(&t);
    *out = v;
    return n;
}
