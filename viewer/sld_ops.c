/* sld_ops - Mastercam solid history records (see sld_ops.h; formats/solid_history.md 2). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sld_ops.h"

static uint32_t rd32(const uint8_t *b) { return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static uint64_t rd64(const uint8_t *b) { return rd32(b) | (uint64_t)rd32(b + 4) << 32; }
static void w32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static void w64(uint8_t *b, uint64_t v) { w32(b, (uint32_t)v); w32(b + 4, (uint32_t)(v >> 32)); }
static double rdd(const uint8_t *b) { double d; memcpy(&d, b, 8); return d; }
static void wdd(uint8_t *b, double d) { memcpy(b, &d, 8); }

/* ---------------------------------------------------------------- reading */
typedef struct { const uint8_t *q, *e; int bad; } Rd;
static int need(Rd *r, size_t n) { if (r->bad || (size_t)(r->e - r->q) < n) { r->bad = 1; return 0; } return 1; }
static uint8_t g8(Rd *r) { if (!need(r, 1)) return 0; return *r->q++; }
static uint32_t g32(Rd *r) { if (!need(r, 4)) return 0; uint32_t v = rd32(r->q); r->q += 4; return v; }
static uint64_t g64(Rd *r) { if (!need(r, 8)) return 0; uint64_t v = rd64(r->q); r->q += 8; return v; }
static int gcount(Rd *r, size_t each) {               /* i32 count, checked against the bytes left */
    int32_t n = (int32_t)g32(r);
    if (n < 0) n = 0;                                 /* FUN_1802649f0 / FUN_18027bbe0: a count <= 0 reads nothing */
    if (r->bad || (size_t)n * each > (size_t)(r->e - r->q)) { r->bad = 1; return 0; }
    return n;
}
static void geptr(Rd *r, SldEptr *x) {                /* 38 bytes: u64 id, u32 kind, u32 op, u64 ref1, u64 ref2, u32, u8, u8 */
    x->id = g64(r); x->kind = g32(r); x->op = g32(r); x->ref1 = g64(r); x->ref2 = g64(r); x->x28 = g32(r); x->b2c = g8(r); x->b2d = g8(r);
}

/* McOperation::Read FUN_1802649f0 (ops version >= 0x19, 64-bit) */
static int r_base(Rd *rr, int type, SldOp *o, char *err, size_t errn) {
    Rd r = *rr;
    o->n1 = gcount(&r, 38); o->e1 = calloc(o->n1 ? o->n1 : 1, sizeof *o->e1); for (int i = 0; i < o->n1; i++) geptr(&r, &o->e1[i]);
    o->n2 = gcount(&r, 38); o->e2 = calloc(o->n2 ? o->n2 : 1, sizeof *o->e2); for (int i = 0; i < o->n2; i++) geptr(&r, &o->e2[i]);
    o->n3 = gcount(&r, 8); o->p3 = calloc(o->n3 ? o->n3 : 1, 8); for (int i = 0; i < o->n3; i++) o->p3[i] = g64(&r);
    uint32_t L = 0; if (need(&r, 2)) { L = r.q[0] | r.q[1] << 8; r.q += 2; }
    if (!need(&r, L)) { snprintf(err, errn, "op name"); return 0; }
    { size_t k = 0; while (k < L && k < sizeof o->name - 1 && r.q[k]) { o->name[k] = (char)r.q[k]; k++; } o->name[k] = 0;
      if (L == 0 || r.q[L - 1] != 0 || strlen(o->name) + 1 != L) { snprintf(err, errn, "op name not strlen + 1 bytes"); return 0; } }
    r.q += L;
    o->f9 = g8(&r); o->f10 = g8(&r); o->f11 = g8(&r); o->f12 = g8(&r);
    o->x10 = g32(&r);
    o->solid_eptr = g64(&r);
    o->id = g32(&r);
    uint32_t t2 = g32(&r);
    o->ver = g32(&r);
    o->x40 = g8(&r); o->x41 = g8(&r);
    if (!r.bad && (int)t2 != type) { snprintf(err, errn, "op type %u inside, %d outside", t2, type); return 0; }
    *rr = r; return 1;
}

int sld_op_read(int type, const uint8_t *body, size_t n, int ver, SldOp *o, char *err, size_t errn) {
    memset(o, 0, sizeof *o); o->type = type; o->fver = ver;
    if (ver < 0x19) { snprintf(err, errn, "ops version 0x%x: only >= 0x19 (64-bit) is handled", ver); return 0; }
    if (type != SLD_OP_EXTRUDE_CUT && type != SLD_OP_EXTRUDE_BOSS && type != SLD_OP_EXTRUDE && type != SLD_OP_FILLET) {
        o->cls = SLD_CLS_RAW; o->raw = malloc(n ? n : 1); memcpy(o->raw, body, n); o->nraw = n; return 1;
    }
    Rd r = { body, body + n, 0 };
    if (type == SLD_OP_FILLET) {
        o->cls = SLD_CLS_FILLET;
        if (!r_base(&r, type, o, err, errn)) { sld_op_free(o); return 0; }
        /* McSldEntOp::Read FUN_180274c80 */
        o->ex90 = g8(&r);
        o->na = gcount(&r, 8 + 0x18); o->ida = calloc(o->na ? o->na : 1, 8); o->pta = calloc(o->na ? o->na : 1, sizeof *o->pta);
        for (int i = 0; i < o->na; i++) o->ida[i] = g64(&r);
        for (int i = 0; i < o->na; i++) for (int k = 0; k < 3; k++) if (need(&r, 8)) { o->pta[i][k] = rdd(r.q); r.q += 8; }
        o->nb = gcount(&r, 8 + 0x18); o->idb = calloc(o->nb ? o->nb : 1, 8); o->ptb = calloc(o->nb ? o->nb : 1, sizeof *o->ptb);
        for (int i = 0; i < o->nb; i++) o->idb[i] = g64(&r);
        for (int i = 0; i < o->nb; i++) for (int k = 0; k < 3; k++) if (need(&r, 8)) { o->ptb[i][k] = rdd(r.q); r.q += 8; }
        o->exc0 = g8(&r); o->bfl = calloc(o->nb ? o->nb : 1, 1);
        if (o->nb > 0 && o->exc0 && need(&r, (size_t)o->nb)) { memcpy(o->bfl, r.q, (size_t)o->nb); r.q += o->nb; }
        o->n48 = gcount(&r, 0x48); o->r48 = malloc((size_t)o->n48 * 0x48 + 1); if (o->n48) { memcpy(o->r48, r.q, (size_t)o->n48 * 0x48); r.q += (size_t)o->n48 * 0x48; }
        o->n60 = gcount(&r, 0x60); o->r60 = malloc((size_t)o->n60 * 0x60 + 1); if (o->n60) { memcpy(o->r60, r.q, (size_t)o->n60 * 0x60); r.q += (size_t)o->n60 * 0x60; }
        /* SldFilletOp::Read FUN_180281170: P 0x18 (SysFile version > 9), i32 n_vr, the items */
        if (need(&r, 0x18)) { memcpy(o->FP, r.q, 0x18); r.q += 0x18; }
        o->nvr = gcount(&r, 4); o->vrk = calloc(o->nvr ? o->nvr : 1, sizeof(int));
        o->vra = calloc(o->nvr ? o->nvr : 1, sizeof(uint64_t *)); o->vrb = calloc(o->nvr ? o->nvr : 1, sizeof(uint64_t *));
        for (int i = 0; i < o->nvr && !r.bad; i++) {
            int k = gcount(&r, 16); o->vrk[i] = k; o->vra[i] = calloc(k ? k : 1, 8); o->vrb[i] = calloc(k ? k : 1, 8);
            for (int j = 0; j < k; j++) o->vra[i][j] = g64(&r);
            for (int j = 0; j < k; j++) o->vrb[i][j] = g64(&r);
        }
        if (r.bad) { snprintf(err, errn, "fillet op truncated"); sld_op_free(o); return 0; }
        if (r.q != r.e) { snprintf(err, errn, "fillet op: %zu bytes left (Mastercam: sld 259 size mismatch)", (size_t)(r.e - r.q)); sld_op_free(o); return 0; }
        return 1;
    }
    o->cls = SLD_CLS_EXTRUDE;
    if (!r_base(&r, type, o, err, errn)) { sld_op_free(o); return 0; }
    /* McChainOp::Read FUN_180271cc0 */
    o->x90 = g8(&r);
    o->nch = gcount(&r, 4 + 4 + 1 + 1);
    int nc = o->nch ? o->nch : 1;
    o->cnt = calloc(nc, sizeof(int)); o->xa0 = calloc(nc, sizeof(int32_t)); o->ent = calloc(nc, sizeof(uint64_t *));
    o->xc0 = calloc(nc, 1); o->xa8 = calloc(nc, 1); o->rev = calloc(nc, sizeof(uint8_t *)); o->cdata = calloc(nc, sizeof *o->cdata);
    for (int c = 0; c < o->nch; c++) { int32_t k = (int32_t)g32(&r); if (k < 0 || (size_t)k * 9 > (size_t)(r.e - r.q)) { r.bad = 1; k = 0; } o->cnt[c] = k; }
    for (int c = 0; c < o->nch; c++) o->xa0[c] = (int32_t)g32(&r);
    for (int c = 0; c < o->nch; c++) { o->ent[c] = calloc(o->cnt[c] ? o->cnt[c] : 1, 8); for (int k = 0; k < o->cnt[c]; k++) o->ent[c][k] = g64(&r); }
    for (int c = 0; c < o->nch; c++) o->xc0[c] = g8(&r);
    for (int c = 0; c < o->nch; c++) o->xa8[c] = g8(&r);
    for (int c = 0; c < o->nch; c++) { o->rev[c] = calloc(o->cnt[c] ? o->cnt[c] : 1, 1); for (int k = 0; k < o->cnt[c]; k++) o->rev[c][k] = g8(&r); }
    /* SldExtrudeOp::Read FUN_18027bbe0 */
    if (need(&r, 0x70)) { memcpy(o->P, r.q, 0x70); r.q += 0x70; }
    o->ntrim = gcount(&r, 8 + 0x18);
    o->trim = calloc(o->ntrim ? o->ntrim : 1, 8); for (int i = 0; i < o->ntrim; i++) o->trim[i] = g64(&r);
    o->trim_data = calloc(o->ntrim ? o->ntrim : 1, 0x18); if (o->ntrim && need(&r, (size_t)o->ntrim * 0x18)) { memcpy(o->trim_data, r.q, (size_t)o->ntrim * 0x18); r.q += (size_t)o->ntrim * 0x18; }
    for (int c = 0; c < o->nch; c++) for (int k = 0; k < 12; k++) { if (need(&r, 8)) { o->cdata[c][k] = rdd(r.q); r.q += 8; } }
    if (r.bad) { snprintf(err, errn, "extrude op truncated"); sld_op_free(o); return 0; }
    if (r.q != r.e) { snprintf(err, errn, "extrude op: %zu bytes left (Mastercam: sld 259 / 225 size mismatch)", (size_t)(r.e - r.q)); sld_op_free(o); return 0; }
    return 1;
}

void sld_op_free(SldOp *o) {
    if (!o) return;
    free(o->e1); free(o->e2); free(o->p3);
    for (int c = 0; c < o->nch; c++) { if (o->ent) free(o->ent[c]); if (o->rev) free(o->rev[c]); }
    free(o->cnt); free(o->xa0); free(o->ent); free(o->xc0); free(o->xa8); free(o->rev); free(o->cdata);
    free(o->trim); free(o->trim_data); free(o->raw);
    free(o->ida); free(o->pta); free(o->idb); free(o->ptb); free(o->bfl); free(o->r48); free(o->r60);
    for (int i = 0; i < o->nvr; i++) { if (o->vra) free(o->vra[i]); if (o->vrb) free(o->vrb[i]); }
    free(o->vrk); free(o->vra); free(o->vrb);
    memset(o, 0, sizeof *o);
}

/* ---------------------------------------------------------------- writing */
typedef struct { uint8_t *b; size_t n, cap; } Wb;
static void wput(Wb *w, const void *p, size_t n) {
    if (w->n + n > w->cap) { w->cap = (w->n + n) * 2 + 64; w->b = realloc(w->b, w->cap); }
    memcpy(w->b + w->n, p, n); w->n += n;
}
static void p8(Wb *w, uint8_t v) { wput(w, &v, 1); }
static void p32(Wb *w, uint32_t v) { uint8_t b[4]; w32(b, v); wput(w, b, 4); }
static void p64(Wb *w, uint64_t v) { uint8_t b[8]; w64(b, v); wput(w, b, 8); }
static void pd(Wb *w, double v) { uint8_t b[8]; wdd(b, v); wput(w, b, 8); }
static void peptr(Wb *w, const SldEptr *x) { p64(w, x->id); p32(w, x->kind); p32(w, x->op); p64(w, x->ref1); p64(w, x->ref2); p32(w, x->x28); p8(w, x->b2c); p8(w, x->b2d); }

/* McOperation write FUN_180266560 (ops version >= 0x19, 64-bit): the three lists, u16 strlen + 1 + name, op+9..+0xc, +0x10, +0x18,
   +0x34, +0x30, +0x3c, +0x40, +0x41 */
static void w_base(Wb *w, const SldOp *o) {
    p32(w, (uint32_t)o->n1); for (int i = 0; i < o->n1; i++) peptr(w, &o->e1[i]);
    p32(w, (uint32_t)o->n2); for (int i = 0; i < o->n2; i++) peptr(w, &o->e2[i]);
    p32(w, (uint32_t)o->n3); for (int i = 0; i < o->n3; i++) p64(w, o->p3[i]);
    size_t L = strlen(o->name) + 1; uint8_t l2[2] = { (uint8_t)L, (uint8_t)(L >> 8) }; wput(w, l2, 2); wput(w, o->name, L);
    p8(w, o->f9); p8(w, o->f10); p8(w, o->f11); p8(w, o->f12);
    p32(w, o->x10); p64(w, o->solid_eptr); p32(w, o->id); p32(w, (uint32_t)o->type); p32(w, o->ver); p8(w, o->x40); p8(w, o->x41);
}

uint8_t *sld_op_write(const SldOp *o, size_t *n) {
    *n = 0;
    if (o->cls == SLD_CLS_RAW) { uint8_t *b = malloc(o->nraw ? o->nraw : 1); memcpy(b, o->raw, o->nraw); *n = o->nraw; return b; }
    if ((o->cls != SLD_CLS_EXTRUDE && o->cls != SLD_CLS_FILLET) || o->fver < 0x19) return NULL;
    Wb w = { 0 };
    w_base(&w, o);
    if (o->cls == SLD_CLS_FILLET) {
        /* McSldEntOp (the inverse of FUN_180274c80) + SldFilletOp write FUN_180281c70 */
        p8(&w, o->ex90);
        p32(&w, (uint32_t)o->na); for (int i = 0; i < o->na; i++) p64(&w, o->ida[i]);
        for (int i = 0; i < o->na; i++) for (int k = 0; k < 3; k++) pd(&w, o->pta[i][k]);
        p32(&w, (uint32_t)o->nb); for (int i = 0; i < o->nb; i++) p64(&w, o->idb[i]);
        for (int i = 0; i < o->nb; i++) for (int k = 0; k < 3; k++) pd(&w, o->ptb[i][k]);
        p8(&w, o->exc0); if (o->nb > 0 && o->exc0) wput(&w, o->bfl, (size_t)o->nb);
        p32(&w, (uint32_t)o->n48); if (o->n48) wput(&w, o->r48, (size_t)o->n48 * 0x48);
        p32(&w, (uint32_t)o->n60); if (o->n60) wput(&w, o->r60, (size_t)o->n60 * 0x60);
        wput(&w, o->FP, 0x18);
        p32(&w, (uint32_t)o->nvr);
        for (int i = 0; i < o->nvr; i++) { p32(&w, (uint32_t)o->vrk[i]); for (int j = 0; j < o->vrk[i]; j++) p64(&w, o->vra[i][j]); for (int j = 0; j < o->vrk[i]; j++) p64(&w, o->vrb[i][j]); }
        *n = w.n; return w.b;
    }
    /* McChainOp: u8, i32 n, i32 count[n], i32 xa0[n], the eptrs, u8 xc0[n], u8 xa8[n], the reversed flags */
    p8(&w, o->x90); p32(&w, (uint32_t)o->nch);
    for (int c = 0; c < o->nch; c++) p32(&w, (uint32_t)o->cnt[c]);
    for (int c = 0; c < o->nch; c++) p32(&w, (uint32_t)o->xa0[c]);
    for (int c = 0; c < o->nch; c++) for (int k = 0; k < o->cnt[c]; k++) p64(&w, o->ent[c][k]);
    for (int c = 0; c < o->nch; c++) p8(&w, o->xc0[c]);
    for (int c = 0; c < o->nch; c++) p8(&w, o->xa8[c]);
    for (int c = 0; c < o->nch; c++) wput(&w, o->rev[c], (size_t)o->cnt[c]);
    /* SldExtrudeOp write FUN_18027cb40: P (0x70), i32 ntrim + ntrim u64 + ntrim x 0x18, per chain 0x60 bytes of chain data */
    wput(&w, o->P, 0x70);
    p32(&w, (uint32_t)o->ntrim);
    for (int i = 0; i < o->ntrim; i++) p64(&w, o->trim[i]);
    if (o->ntrim) wput(&w, o->trim_data, (size_t)o->ntrim * 0x18);
    for (int c = 0; c < o->nch; c++) for (int k = 0; k < 12; k++) pd(&w, o->cdata[c][k]);
    *n = w.n; return w.b;
}

uint8_t *sld_opsfile_write(const SldOpsHdr *h, const SldOp *ops, int nops, size_t *n) {
    Wb w = { 0 };
    /* FUN_18026dc50: u8 +0xc, u8 +0xe, u32 +0x10, u32 +0x14, u64 +0x20 (u32 below file version 0x11), u32 count */
    p8(&w, h->regen); p8(&w, h->expanded); p32(&w, h->solid_id); p32(&w, h->pos); p64(&w, h->solid_eptr); p32(&w, (uint32_t)nops);
    for (int i = 0; i < nops; i++) {
        size_t k; uint8_t *b = sld_op_write(&ops[i], &k);
        if (!b) { free(w.b); *n = 0; return NULL; }
        p32(&w, (uint32_t)ops[i].type); p32(&w, (uint32_t)k); wput(&w, b, k); free(b);
    }
    *n = w.n; return w.b;
}

/* ---------------------------------------------------------------- SLD_EXTRUDE_PARAMS */
void sld_extp_get(const uint8_t *P, SldExtP *p) {
    p->type = (int32_t)rd32(P); p->target = rd64(P + 8); p->method = (int32_t)rd32(P + 0x10); p->dist = rdd(P + 0x18);
    for (int k = 0; k < 3; k++) p->vec[k] = rdd(P + 0x20 + 8 * k);
    p->trim = P[0x38]; p->reverse = P[0x39]; p->both = P[0x3a]; p->split = P[0x3b];
    p->draft = P[0x40]; p->draft_rev = (int32_t)rd32(P + 0x44); p->draft_ang = rdd(P + 0x48);
    p->thin = P[0x50]; p->thin_dir = (int32_t)rd32(P + 0x54); p->thick = rdd(P + 0x58); p->thick2 = rdd(P + 0x60); p->draft_ends = P[0x68];
}
void sld_extp_put(uint8_t *P, const SldExtP *p) {
    w32(P, (uint32_t)p->type); w64(P + 8, p->target); w32(P + 0x10, (uint32_t)p->method); wdd(P + 0x18, p->dist);
    for (int k = 0; k < 3; k++) wdd(P + 0x20 + 8 * k, p->vec[k]);
    P[0x38] = (uint8_t)p->trim; P[0x39] = (uint8_t)p->reverse; P[0x3a] = (uint8_t)p->both; P[0x3b] = (uint8_t)p->split;
    P[0x40] = (uint8_t)p->draft; w32(P + 0x44, (uint32_t)p->draft_rev); wdd(P + 0x48, p->draft_ang);
    P[0x50] = (uint8_t)p->thin; w32(P + 0x54, (uint32_t)p->thin_dir); wdd(P + 0x58, p->thick); wdd(P + 0x60, p->thick2); P[0x68] = (uint8_t)p->draft_ends;
}

void sld_filp_get(const uint8_t *P, SldFilP *p) {
    p->type = (int32_t)rd32(P); p->radius = rdd(P + 8); p->overflow = (int32_t)rd32(P + 0x10); p->mitered = P[0x14]; p->propagate = P[0x15];
}
void sld_filp_put(uint8_t *P, const SldFilP *p) {
    w32(P, (uint32_t)p->type); wdd(P + 8, p->radius); w32(P + 0x10, (uint32_t)p->overflow); P[0x14] = (uint8_t)p->mitered; P[0x15] = (uint8_t)p->propagate;
}

void sld_new_free(SldNew *s, int n) {
    for (int i = 0; i < n; i++) { for (int k = 0; k < s[i].nops; k++) sld_op_free(&s[i].ops[k]); free(s[i].ops); }
    free(s);
}
