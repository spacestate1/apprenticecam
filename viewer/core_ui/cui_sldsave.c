/* cui_sldsave.c - the solids made here as Mastercam writes them (user decision SOL-1, formats/solid_history.md 5): for every body of the
   Extrude features (cui_feat.c sbodies, on OpenCASCADE) its history = one SldExtrudeOp per op of the body (formats/extrude.md 6), built from
   the feature the op came from (ocs_op_tag) and its group (ocs_op_group), with the McEptr names the kernel made (occt_solid.cpp,
   solid_history.md 3.3 / 3.4). mcam_saveops.c writes them (SOLID_ID + SOLIDXMT + XT + trailer + McOpsFile). A body whose ops' names are NOT
   DECODED (Both directions, Draft, Thin wall, primitives) is refused with the reason. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "sld_ops.h"
#include "mcam_saveops.h"

/* a record's id: Mastercam writes the record's own address at save time (FUN_18026c690: +0x20 = this), consecutive at 0x30 inside one list
   (one block, malloc(n * 0x38)): 2,391 / 2,391 corpus ops. Any unique values are equivalent (the reader only matches ids, FUN_18026a240);
   these are ours: one block per op and list */
static uint64_t rec_id(int op, int list, int i) { return 0x10000000ull + ((uint64_t)op << 22) + ((uint64_t)list << 21) + 0x30ull * (uint64_t)i; }

/* -0.0 where Mastercam negates: the direction of a -Z chain is -(0, 0, 1) = (-0.0, -0.0, -1.0) in every stored op (test-cube: P+0x20 and the
   chain data), so a direction is made the same way */
static void zdir(int s, double d[3]) { d[0] = 0; d[1] = 0; d[2] = 1; if (s < 0) { d[0] = -d[0]; d[1] = -d[1]; d[2] = -d[2]; } }

/* one op of body b from feature f: chains = the group's (grp >= 0: the root loop, then its holes, ex_grouping order) or all the feature's
   (grp -1: the single-op path FUN_1800d2c80) */
static int build_op(const Body *bd, int k, const ExFeat *f, int grp, SldOp *o, char *why, size_t wn) {
    memset(o, 0, sizeof *o);
    o->type = sld_optype(f); o->cls = SLD_CLS_EXTRUDE;
    snprintf(o->name, sizeof o->name, "%s", f->name[0] ? f->name : ex_defname(f->p.type));
    o->id = (uint32_t)k; o->x40 = 0; o->x41 = 12;            /* +0x40 / +0x41: 0 / 12 in all 2,988 corpus extrude ops (NOT DECODED) */
    if (f->fl) {                                             /* SldFilletOp (formats/solid_fillet.md 6): +0x40 / +0x41 = 0 / 12 in all 589 ops */
        o->cls = SLD_CLS_FILLET;
        snprintf(o->name, sizeof o->name, "%s", f->name[0] ? f->name : "Constant Radius Fillet");
        goto names;                                          /* the McSldEntOp lists and P after the names (they need the record ids) */
    }
    /* the chains */
    int *ix = NULL, nix = 0;
    if (grp >= 0) {
        ExGrp *gr = NULL; int ngr = 0; ExErr e;
        if (!ex_grouping(f, &gr, &ngr, &e) || grp >= ngr) { snprintf(why, wn, "the op's chain group is gone"); for (int i = 0; i < ngr; i++) free(gr[i].ix); free(gr); return 0; }
        ix = malloc(sizeof(int) * (gr[grp].n ? gr[grp].n : 1)); nix = gr[grp].n; memcpy(ix, gr[grp].ix, sizeof(int) * nix);
        for (int i = 0; i < ngr; i++) free(gr[i].ix);
        free(gr);
    } else { ix = malloc(sizeof(int) * (f->nch ? f->nch : 1)); for (int i = 0; i < f->nch; i++) ix[i] = i; nix = f->nch; }
    int nc = nix ? nix : 1;
    o->nch = nix; o->cnt = calloc(nc, sizeof(int)); o->xa0 = calloc(nc, sizeof(int32_t)); o->ent = calloc(nc, sizeof(uint64_t *));
    o->xc0 = calloc(nc, 1); o->xa8 = calloc(nc, 1); o->rev = calloc(nc, sizeof(uint8_t *)); o->cdata = calloc(nc, sizeof *o->cdata);
    for (int c = 0; c < nix; c++) {
        const Chain *ch = &f->ch[ix[c]];
        o->cnt[c] = ch->n; o->ent[c] = calloc(ch->n ? ch->n : 1, 8); o->rev[c] = calloc(ch->n ? ch->n : 1, 1);
        for (int q = 0; q < ch->n; q++) { o->ent[c][q] = MCAM_EPTR_TOKEN(ch->ent[q]); o->rev[c][q] = ch->rev[q] ? 1 : 0; }
        /* op+0xa0 per chain = the chain's first entity's reversed flag (data: 2,894 / 2,894 corpus chains; the setter is NOT DECODED) */
        o->xa0[c] = ch->n && ch->rev[0] ? 1 : 0;
        /* SLD_EXT_CHAIN_DATA: the chain's direction, then the Top plane (origin, normal +Z, x axis +X) - 311 / 311 user chains (formats/
           solid_history.md 2.5) */
        double d[3]; zdir(f->cdir && f->cdir[ix[c]] < 0 ? -1 : 1, d);
        double cd[12] = { d[0], d[1], d[2], 0, 0, 0, 0, 0, 1, 1, 0, 0 };
        memcpy(o->cdata[c], cd, sizeof cd);
    }
    /* SLD_EXTRUDE_PARAMS (formats/extrude.md sec. 0); the vector = the first chain's direction (4.1); the target is the save's */
    SldExtP P; memset(&P, 0, sizeof P);
    P.type = f->p.type; P.method = f->p.method; P.dist = f->p.dist; zdir(nix && f->cdir && f->cdir[ix[0]] < 0 ? -1 : 1, P.vec);
    P.trim = f->p.trim; P.reverse = f->p.reverse; P.both = f->p.both; P.split = f->p.split; P.draft = f->p.draft; P.draft_rev = f->p.draft_rev;
    P.draft_ang = f->p.draft_ang * 0.01745329251994329;     /* degrees on the panel, radians in P (FUN_1801308a0) */
    P.thin = f->p.thin; P.thin_dir = f->p.thin_dir; P.thick = f->p.thick; P.thick2 = f->p.thick2; P.draft_ends = f->p.draft_ends;
    sld_extp_put(o->P, &P);
    free(ix);
names:;
    /* the names (McOperation +0x44 / +0x60 / +0x80) */
    int nrec = ocs_nrecs(bd->occ);
    uint64_t *id = calloc(nrec ? nrec : 1, sizeof(uint64_t));
    for (int kk = 0; kk < ocs_nops(bd->occ); kk++) for (int l = 0; l < 2; l++) {
        const int *x; int n = ocs_op_list(bd->occ, kk, l, &x);
        for (int i = 0; i < n; i++) if (x[i] >= 0 && x[i] < nrec) id[x[i]] = rec_id(kk, l, i);
    }
    int ok = 1;
    for (int l = 0; l < 3 && ok; l++) {
        const int *x; int n = ocs_op_list(bd->occ, k, l, &x);
        if (l == 2) {
            o->n3 = n; o->p3 = calloc(n ? n : 1, 8);
            for (int i = 0; i < n; i++) { o->p3[i] = id[x[i]]; if (!o->p3[i]) ok = 0; }
            continue;
        }
        SldEptr *e = calloc(n ? n : 1, sizeof *e);
        for (int i = 0; i < n; i++) {
            const OcsRec *r = ocs_rec(bd->occ, x[i]);
            e[i].id = rec_id(k, l, i); e[i].kind = r->kind; e[i].op = (uint32_t)r->op;
            e[i].ref1 = r->r1t == OCS_REF_ENT ? MCAM_EPTR_TOKEN(r->r1) : r->r1t == OCS_REF_REC ? id[r->r1] : 0;
            e[i].ref2 = r->r2t == OCS_REF_ENT ? MCAM_EPTR_TOKEN(r->r2) : r->r2t == OCS_REF_REC ? id[r->r2] : 0;
            if ((r->r1t == OCS_REF_REC && !e[i].ref1) || (r->r2t == OCS_REF_REC && !e[i].ref2)) ok = 0;
        }
        if (l == 0) { o->n1 = n; o->e1 = e; } else { o->n2 = n; o->e2 = e; }
    }
    if (f->fl) {
        /* McSldEntOp (sld_fillet's new-op path, formats/solid_fillet.md 6.2): whole body -> op+0x90 = 1 and both lists empty; else list A =
           the picked faces, list B = the picked edges: the record id of each (mcEptrPtr; 0 = none, 33 corpus edges) + its bbox centre */
        const FilFeat *q = f->fl;
        o->ex90 = q->whole ? 1 : 0;
        int na = q->whole ? 0 : q->nf, nb = q->whole ? 0 : q->ne;
        o->na = na; o->ida = calloc(na + 1, 8); o->pta = calloc(na + 1, sizeof *o->pta);
        o->nb = nb; o->idb = calloc(nb + 1, 8); o->ptb = calloc(nb + 1, sizeof *o->ptb); o->bfl = calloc(nb + 1, 1);
        for (int i = 0; i < na; i++) { o->ida[i] = q->frec[i] >= 0 && q->frec[i] < nrec ? id[q->frec[i]] : 0; memcpy(o->pta[i], q->fpt[i], sizeof o->pta[i]); }
        for (int i = 0; i < nb; i++) { o->idb[i] = q->erec[i] >= 0 && q->erec[i] < nrec ? id[q->erec[i]] : 0; memcpy(o->ptb[i], q->ept[i], sizeof o->ptb[i]); }
        o->r48 = calloc(1, 1); o->r60 = calloc(1, 1);
        /* SLD_FILLET_PARAMS: type 0 constant (FUN_180129160 writes 0), the panel's values; padding 0 as in the 2017 op of the corpus */
        SldFilP fp = { 0, q->radius, q->overflow, q->mitered, q->propagate };
        memset(o->FP, 0, sizeof o->FP); sld_filp_put(o->FP, &fp);
        o->nvr = 0; o->vrk = calloc(1, sizeof(int)); o->vra = calloc(1, sizeof(uint64_t *)); o->vrb = calloc(1, sizeof(uint64_t *));
    }
    free(id);
    o->trim = calloc(1, 8); o->trim_data = calloc(1, 0x18);
    if (!ok) { snprintf(why, wn, "internal: a name refers to a record of no list"); return 0; }
    return 1;
}

/* an imported solid (STEP, cad_import.c: F.solids[i].brep) as Mastercam keeps it: a "brick" = one SldBodyOp (formats/solid_import_save.md):
   sld_import_solid -> sld_import_add_mc (mcgeomsld 0x1800bd390) -> sld_solid_from_tag -> dbput_solid, then mc_create_body_op (0x18002b610):
   the SldBodyOp (ctor FUN_18026e130 over McOperation FUN_180261b90: name "", +0x3c 0x1c, +0x40 / +0x41 = 0 / 0x0c), +0x18 = the solid,
   the records from ps_import_solid (FUN_1802656b0) and the body copy (FUN_18026fa70). The records and the XT copy depend on the XT the save
   writes (its edges / faces), so mcam_saveops.c fills them; the op version is the file's (as for every op written here, mcam_saveops.c) */
static int add_imported(SldNew *n, const Solid *S) {
    n->imported = 1; n->level = S->level; n->color = S->color; n->expanded = S->expanded;
    snprintf(n->desc, sizeof n->desc, "%s", S->desc);
    n->body = ocs_from_brep(S->brep, S->brep_n);
    if (!n->body) { snprintf(n->why, sizeof n->why, "the imported solid's BRep cannot be read"); return 0; }
    n->ops = calloc(1, sizeof(SldOp)); n->nops = 1;
    SldOp *o = &n->ops[0];
    o->type = SLD_OP_BODY; o->cls = SLD_CLS_BODY; o->name[0] = 0; o->id = 0; o->x40 = 0; o->x41 = 0x0c;
    return 1;
}

void sld_new_drop(SldNew *s, int n) {
    for (int i = 0; s && i < n; i++) if (s[i].imported && s[i].body) { ocs_unref(s[i].body); s[i].body = NULL; }
    sld_new_free(s, n);
}

/* the solids to write (F.sldnew); returns the count. Every body is listed: a refused one has why set. The imported solids follow the bodies
   made here. */
int sld_new_build(SldNew **out) {
    *out = NULL;
    int nimp = 0; for (int i = 0; loaded && i < F.nsolids; i++) if (F.solids[i].brep) nimp++;
    if (nsbodies <= 0 && !nimp) return 0;
    SldNew *s = calloc((size_t)(nsbodies > 0 ? nsbodies : 0) + (size_t)nimp, sizeof(SldNew));
    int nb = nsbodies > 0 ? nsbodies : 0;
    for (int i = 0, k = nb; loaded && i < F.nsolids; i++) if (F.solids[i].brep) add_imported(&s[k++], &F.solids[i]);
    for (int b = 0; b < nsbodies; b++) {
        const Body *bd = &sbodies[b]; SldNew *n = &s[b];
        /* SOL-5d: the level / colour the body got at creation (its first op's feature, ExFeat.level / color = dbput_solid's main_level /
           solidColor); an older project without them: the current ones */
        { int t0 = bd->occ && ocs_nops(bd->occ) ? ocs_op_tag(bd->occ, 0) : -1;
          n->level = t0 >= 0 && t0 < nfeats && feats[t0].level ? feats[t0].level : geo_level;
          n->color = t0 >= 0 && t0 < nfeats && feats[t0].color ? feats[t0].color : cfg_solid_color; }
        n->expanded = (uint8_t)(b < 512 && sm_body_init ? sm_body_open[b] != 0 : 1);
        if (!bd->occ) { snprintf(n->why, sizeof n->why, "not an OpenCASCADE body"); continue; }
        n->body = bd->occ;
        int no = ocs_nops(bd->occ);
        n->ops = calloc(no ? no : 1, sizeof(SldOp));
        for (int k = 0; k < no; k++) {
            char w[200] = "";
            if (!ocs_op_named(bd->occ, k, w, sizeof w)) { snprintf(n->why, sizeof n->why, "op %d: its face / edge names are NOT DECODED (%s)", k + 1, w); break; }
            int t = ocs_op_tag(bd->occ, k);
            if (t < 0 || t >= nfeats || feats[t].prim) { snprintf(n->why, sizeof n->why, "op %d: %s", k + 1, t >= 0 && t < nfeats ? "a primitive (SldPrimitiveOp NOT DECODED)" : "no feature"); break; }
            if (!build_op(bd, k, &feats[t], ocs_op_group(bd->occ, k), &n->ops[k], w, sizeof w)) { snprintf(n->why, sizeof n->why, "op %d: %s", k + 1, w); n->nops = k + 1; break; }
            n->nops = k + 1;
        }
    }
    *out = s;
    return nb + nimp;
}
