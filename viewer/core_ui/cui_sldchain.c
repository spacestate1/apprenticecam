/* cui_sldchain.c - solid chaining: the Chaining dialog's Solids mode (mccore CChainDlg with template 6518) and the solid
 * selection behind it (mcgeomsld sld_sel_solid_chains -> sld_array_pick_loops, OnSolidsSelect*, SetSolidChainingUseFace,
 * the Pick Reference Face dialog 134, SldToolPOp::ExtractToolPChainInput / sld_loop_to_chain). The rules and their
 * citations are formats/chaining.md "Solid chaining"; the decompiles are decomp/mcgeomsld_solidchain.c and
 * decomp/mccore_chain_range.c. Edges come from cui_snap.c's edge sets (solid25.c body_brep_edges for the bodies made
 * here, xt_mesh.c xt_brep_edges for the part's Parasolid solids), faces / loops / fins from body_brep_topo /
 * xt_brep_topo (btopo.h). The chains are ordinary Chains of line / arc entities appended to F.ents with Ent.gen set
 * (ps_edge_to_mc_curve stores the edge curves in the database as entities, store_ent_nodraw) - the toolpaths take them
 * like wireframe chains, at the edges' own Z. */
#include "core_ui.h"
#include "occt_cad.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------------------------- state */
int sld_mode;                  /* CChainDlg +0x17f8: the dialog is 6518 and the picks go to the solid selection */
int sld_last_mode;             /* DAT_1813c4bb2: the last chaining ended in Solids mode (FUN_1801edde0 sets, FUN_1801ee2b0 clears) */
/* mcgeomsld selection state (session globals; .data initial values in mcgeomsld.dll): the Edges / Faces / Loops buttons
   checked (DAT_1803baa57 / 56 / 62 = 1), Partial (DAT_1803e80f0) and Back (DAT_1803e80f1) not (bss), contiguous edges
   allowed (DAT_1803baa8b = 1), finish contiguous (DAT_1803e8115, bss 0), the "End chain" prompt flag (DAT_1803e8114),
   SolidChainingUseFace (DAT_1803e80dc, bss 0 = the 3D radio), the last reference face (DAT_1803e80e4) */
int sld_b_edges = 1, sld_b_faces = 1, sld_b_loops = 1, sld_b_partial, sld_b_back;
int sld_allow_contig = 1, sld_finish_contig, sld_contig_wait, sld_use_face;
static int sld_ref_b = -1, sld_ref_f = -1;
/* the dialog's check boxes as FUN_1801f00d0 last set them (+0x17fc Edge, +0x1800 Face, +0x1804 Loop, +0x1808 Partial,
   +0x180c From back) and the Linked edges check box's own state (member 0x1e0d0, HWND +0x1e110) */
int sd_edge, sd_face, sd_loop, sd_part, sd_back, sd_linked;

/* the selection (the CArrays sld_array_pick_loops fills: edges 0x1803e8200 + their flags, faces 0x1803e8228 + flags, loops
   SLD_LOOP_INP 0x1803e8250, the pick-kind order 0x1803e82c8, the contiguous-edge counts 0x1803e82f0) as items in pick
   order: 2 = a run of edges (one chain), 4 = a face (a chain per loop), 8 = a loop / partial loop */
typedef struct {
    int kind, b;
    int L, s, t, rev;          /* loop: loop number, stored start fin (position in the loop), partial end fin or -1, SLD_LOOP_INP +5 */
    int f;                     /* face */
    int ne, *e, *al;           /* edge run: edge numbers and "traversed along the edge" */
} SItem;
static SItem *it; static int nit, itcap;
static SItem *last_it; static int nlast_it;        /* the previous selection (FUN_18009fb70 keeps it for Last) */
static int last_kind;                              /* DAT_1803e80f8: the kind of the last pick (2 / 4 / 8, 0 none) */
static int sld_base;                               /* cchains index where this solid selection's chains start */
static int lastc0 = -1, lastc1 = -1;               /* the chains of the last item (its arrows) */
static int gen_from = -1;                          /* F.nents before this selection's curves (they are rebuilt on every change) */
/* the pending picks: the Pick Reference Face dialog (FUN_1800a0dd0 _sld_select_ref_face, SRefFaceDlg 134) and the
   second pick of a partial loop (prompt 450, FUN_1800c2af0) */
static int ref_open; static SItem ref_it; static int ref_fin[2], ref_ix, ref_N, ref_mode;
static int part_wait; static SItem part_it;
int sld_hover_b = -1, sld_hover_e = -1;            /* the edge under the cursor (the solid hover highlight) */

/* ---------------------------------------------------------------------------------------------- topology cache */
static BTopo *tp; static int ntp, tp_gen = -1;
static int **efin; static int *nefin;              /* per set: per edge the two fins (-1 when missing) */
static const BTopo *topo(int b) {
    int n = ac_solid_sets();
    if (tp_gen != ac_solids_gen || ntp != n) {
        for (int i = 0; i < ntp; i++) { btopo_free(&tp[i]); free(efin[i]); }
        free(tp); free(efin); free(nefin); tp = NULL; efin = NULL; nefin = NULL; ntp = 0;
        tp = calloc(n ? n : 1, sizeof(BTopo)); efin = calloc(n ? n : 1, sizeof(int *)); nefin = calloc(n ? n : 1, sizeof(int));
        int nb = ac_solid_nbodies();
        for (int i = 0; i < n; i++) {
            const Body *bd = ac_solid_body(i);
            if (bd) body_brep_topo(bd, &tp[i]);
            else { const Solid *so = &F.solids[i - nb]; if (so->brep) occt_brep_topo(so->brep, so->brep_n, &tp[i]); else { XtSchema *sc = xt_schema_for(so->xt, so->xt_size); if (sc) xt_brep_topo(sc, so->xt, so->xt_size, solid_scale, &tp[i]); } }
            const BEdge *E; int ne = ac_solid_edges(i, &E); nefin[i] = ne;
            efin[i] = malloc(sizeof(int) * 2 * (ne ? ne : 1)); for (int k = 0; k < 2 * ne; k++) efin[i][k] = -1;
            for (int k = 0; k < tp[i].nfin; k++) { int e = tp[i].fin[k].e; if (e < 0 || e >= ne) continue;
                int *s = efin[i] + 2 * e; if (s[0] < 0) s[0] = k; else if (s[1] < 0) s[1] = k; }
        }
        ntp = n; tp_gen = ac_solids_gen;
    }
    return b >= 0 && b < ntp ? &tp[b] : NULL;
}
static int fin_loop(const BTopo *t, int fin) { for (int l = 0; l < t->nloop; l++) if (fin >= t->loop[l].f0 && fin < t->loop[l].f0 + t->loop[l].nf) return l; return -1; }

/* ---------------------------------------------------------------------------------------------- the dialog */
/* mccoreres dialog 6518 "Chaining" (143 x 286 du; exports/mccoreres_dialogs.tsv), FUN_1801f06d0 when +0x17f8 is set and
   not lathe (allow_solid_lathe_chains). Images: OnInitDialog FUN_1801ecfc0's solid branch (SetBitmap on the DDX members of
   FUN_1801ec100, bitmaps loaded by the ctor FUN_1801ead40): Edge 2953 -> 1148, Linked edges 2956 -> 10477, Face 2954 ->
   1149, Loop 2955 -> 1150, Partial loop 2957 -> 1151, From back 2959 -> 1152, Unselect All 10204 -> 10205, End chain
   12321 -> 7004, Back 542 -> 1139, Forward 561 -> 1147, and as in wireframe mode Wireframe 537 -> 1133, Solids 351 ->
   1142, Unselect 541 -> 1216, Reverse 25381 -> 1123, Last 525 -> 1138. 3041 Face / 1499 3D: a radio group (DDX_Radio
   0xbe1 -> +0x17e4 = !GetSolidChainingUseFace()); the template's empty group box around them (32, 36, 78, 21) is left
   out as in 6464 / 1058. Edge / Linked / Face / Loop / Partial / From back are UIBitmapCheckbox members (pressed =
   checked). No expand icon (UIVertDialog::ShowExpandButton(+0x17f8 == 0)). */
const ChCtl CH_6518[] = {
    { 537, 32, 13, 37, 19, 1133, "Wireframe" }, { 351, 75, 13, 37, 19, 1142, "Solids" },
    { 3041, 38, 43, 38, 10, -1, "Face" }, { 1499, 80, 43, 25, 10, -1, "3D" },
    { 2953, 32, 67, 36, 18, 1148, "Edge" }, { 2955, 75, 67, 36, 18, 1150, "Loop" },
    { 2956, 32, 92, 36, 18, 10477, "Linked edges" }, { 2957, 75, 92, 36, 18, 1151, "Partial loop" },
    { 2954, 32, 118, 36, 18, 1149, "Face" }, { 2959, 75, 118, 36, 18, 1152, "From back" },
    { 525, 32, 143, 36, 18, 1138, "Last" }, { 12321, 75, 143, 36, 18, 7004, "End chain" },
    { 541, 32, 170, 36, 18, 1216, "Unselect" }, { 10204, 75, 170, 36, 18, 10205, "Unselect All" },
    { 25381, 53, 197, 36, 18, 1123, "Reverse" },
    { 542, 32, 226, 36, 18, 1139, "Back" }, { 561, 75, 226, 36, 18, 1147, "Forward" },
    { 1, 33, 261, 36, 18, 0, "OK" }, { 9, 74, 261, 36, 18, 0, "Help" },
};
const int CH_6518_N = sizeof CH_6518 / sizeof CH_6518[0];
/* mcgeomsldres dialog 134 "Pick Reference Face" (118 x 49 du, exports/mcgeomsldres_dialogs.tsv), SRefFaceDlg (ctor
   FUN_18005d430: MCDialog 0x86, modeless Create; message map @0x1802d4450: 273 -> FUN_18005d690) */
const ChCtl SREF_134[] = { { 273, 7, 7, 104, 14, 0, "Other face" }, { 1, 37, 26, 36, 18, 0, "OK" }, { 9, 76, 26, 36, 18, 0, "Help" } };
const int SREF_134_N = 3;

/* toolpath chaining with the Solids button live (FUN_1801f00d0: +0x1f0b9 && allow_solid_chains && sld_get_num_solids_visible()
   >= 1). allow_solid_chains is set by sld_init_solid_chains_selection, called before chain_it by the ops that take solid
   chains (decomp/mcmill_all.c): ctour_toolpaths (Contour, chamfer / engrave variants), face_assoc (Facing), slotmill,
   FUN_1802cddc0 (2D High Speed: Dynamic Mill / Area Mill / Dynamic Contour / Peel Mill) and mccore prompt_pocket
   (Pocket, decomp/mccore_sldchain_misc.c); +0x1f0b9 = run_chain_dlg param_6 == 0 && the same two tests (chain_it ->
   param_6 0). The Extrude command and the wireframe chain commands never call it (Solids greyed). Solids visible here =
   the edge sets of the bodies on screen and the part's solids (cui_snap.c). */
int sld_allowed(void) {
    if (!cam_mode || show_params || ex_mode || geo_chaining) return 0;
    if (!(cam_mode == CAM_CONTOUR || cam_mode == CAM_POCKET || cam_mode == CAM_FACE || cam_mode == CAM_SLOT || cam_mode == CAM_DYNAMIC ||
          cam_mode == CAM_AREA || cam_mode == CAM_DYNCONTOUR || cam_mode == CAM_PEEL)) return 0;
    return ac_solid_sets() >= 1;
}

/* FUN_1801f00d0, the solid branch (allow_solid_lathe_chains off; GetSolidSelectionActive is set while the pick waits,
   FUN_1800a44c0(6, 1, 1, 1) from FUN_1800c2da0: CanSelect Edges / Faces / Loops / Partial, ShowReverse, ShowLast all 1) */
void sld_dlg_refresh(void) {
    sd_back = sld_b_back; sd_edge = sld_b_edges; sd_loop = sld_b_loops; sd_part = sld_b_partial; sd_face = sld_b_faces;
    if (!sd_edge && !sd_linked && !sd_part && !sd_loop && !sd_face) { sld_b_faces = !sld_b_faces; sd_face = 1; }   /* OnSolidsSelectFace */
    if (sd_part) { sd_edge = 0; sd_loop = 0; }
    if (sd_loop && !sd_part) sd_edge = 0;
    if (!sd_edge || !sld_allow_contig) { sd_linked = 0; sld_finish_contig = 1; sld_contig_wait = 0; }   /* SetSldFinishContiguousEdges(true) */
    else { sd_linked = sd_edge; sd_edge = 0; }
}
int sld_ctl_live(int id) {
    switch (id) {
    case 537: case 3041: case 1499: case 10204: case 1: case 2953: case 2956: case 2955: case 2957: case 2954: case 2959:
    case 525: case 541: case 25381: return 1;
    case 351: return sld_allowed();
    case 542: case 561: return sd_loop;
    case 12321: return sd_linked;
    }
    return 0;
}
int sld_ctl_on(int id) {
    switch (id) {
    case 351: return 1;
    case 2953: return sd_edge; case 2956: return sd_linked; case 2955: return sd_loop; case 2957: return sd_part;
    case 2954: return sd_face; case 2959: return sd_back;
    case 3041: return sld_use_face; case 1499: return !sld_use_face;
    }
    return 0;
}

/* OnSolidsSelect* (mcgeomsld, decomp/mcgeomsld_solidchain.c; the CheckBoxEnabled variants are off here) */
static void on_edge(void) { int t = !sld_b_edges; sld_b_edges = t; if (!t) { sld_b_loops = 0; sld_b_partial = 0; } }
static void on_loop(void) { sld_b_loops = !sld_b_loops; if (sld_b_loops) sld_b_edges = 1; else sld_b_partial = 0; }
static void on_partial(void) { sld_b_partial = !sld_b_partial; if (sld_b_partial) { sld_b_edges = 1; sld_b_loops = 1; } }
static void on_face(void) { sld_b_faces = !sld_b_faces; }

/* ---------------------------------------------------------------------------------------------- chains from the selection */
static double nc_tol(void) { return mc_systol_units(mcam_is_metric(&F))->nc_tol; }   /* nc_tol (config 1008, McSysTol) */
/* ps_edge_to_mc_curve: the edge's curve as an entity (Ent.gen), in the edge's direction. Lines; circle arcs in XY planes
   (the Ent arc form); other curves cannot be stored here -> -1 */
static int gen_ent(int b, int e, int *flip) {
    const BEdge *EB; int ne = ac_solid_edges(b, &EB); if (e < 0 || e >= ne) return -1;
    const BEdge *E = &EB[e]; if (E->np < 2 || E->curved) return -1;
    const double *P = E->pts, *Q = E->pts + 3 * (E->np - 1);
    *flip = 0;
    Ent en; memset(&en, 0, sizeof en); en.idn = -1; en.gen = 1; en.first_poly = F.npolys; en.ln_style = 1;
    en.a = (V3){ P[0], P[1], P[2] }; en.b = (V3){ Q[0], Q[1], Q[2] };
    if (!E->arc) { en.type = E_LINE; en.cls = 2; en.planar = fabs(P[2] - Q[2]) < 1e-9; }
    else {
        if (fabs(fabs(E->ax[2]) - 1) > 1e-9) return -1;   /* a circle not in an XY plane */
        en.type = E_ARC; en.cls = 4; en.planar = 1; en.c = (V3){ E->c[0], E->c[1], E->c[2] }; en.r = E->r;
        en.a0 = atan2(P[1] - E->c[1], P[0] - E->c[0]);
        const double *P1 = E->pts + 3; double cz = (P[0] - E->c[0]) * (P1[1] - E->c[1]) - (P[1] - E->c[1]) * (P1[0] - E->c[0]);
        en.sw = (cz >= 0 ? 1 : -1) * (E->t1 - E->t0);
        if (E->arc == 2) en.b = en.a;
        /* the arc entity runs counter-clockwise (positive sweep, as the wireframe arcs the toolpaths are written for); a
           clockwise edge is that arc traversed end -> start */
        if (en.sw < 0) { V3 t = en.a; en.a = en.b; en.b = t; en.a0 = atan2(en.a.y - E->c[1], en.a.x - E->c[0]); en.sw = -en.sw; *flip = 1; }
    }
    Ent *ne2 = realloc(F.ents, sizeof(Ent) * (F.nents + 1)); if (!ne2) return -1;
    F.ents = ne2; F.ents[F.nents] = en; return F.nents++;
}
/* add_curve_to_chain for each curve, set_chain_closed(chain, nc_tol) (sld_chains_temp_file_to_chains) */
static int make_chain(int b, const int *e, const int *al, int n, Chain *c) {
    memset(c, 0, sizeof *c); if (n <= 0) return 0;
    c->ent = malloc(sizeof(int) * n); c->rev = malloc(sizeof(int) * n);
    for (int k = 0; k < n; k++) { int flip = 0, g = gen_ent(b, e[k], &flip); if (g < 0) { free(c->ent); free(c->rev); memset(c, 0, sizeof *c); return 0; }
        c->ent[c->n] = g; c->rev[c->n] = (!al[k]) != (flip != 0); c->n++; }
    const Ent *a = &F.ents[c->ent[0]], *z = &F.ents[c->ent[c->n - 1]];
    V3 s = c->rev[0] ? a->b : a->a, t = c->rev[c->n - 1] ? z->a : z->b;
    c->closed = sqrt((s.x - t.x) * (s.x - t.x) + (s.y - t.y) * (s.y - t.y) + (s.z - t.z) * (s.z - t.z)) < nc_tol();
    c->sld = 2;
    return 1;
}
/* sld_loop_to_chain(loop, SLD_LOOP_INP): the fins from the start fin, backward (each against its fin) when +5 (rev) is
   clear, forward (along the fins) when set; with an end edge (+8, partial loop) up to that fin inclusive. For a face
   (start edge 0) the start is fin 0, or fin 1 with rev (more than one fin). An edge's direction along the chain: against
   the fin = against the edge when the fin is positive (PK_FIN_is_positive) */
static int loop_chain(int b, int L, int s, int t, int rev, int face, Chain *c) {
    const BTopo *T = topo(b); if (!T || L < 0 || L >= T->nloop) return 0;
    int n = T->loop[L].nf; const TFin *fn = T->fin + T->loop[L].f0;
    if (rev && t < 0 && !face) s++;                    /* a start edge, rev, no end edge: the fin after it (iVar3 = index + 1) */
    int *e = malloc(sizeof(int) * n), *al = malloc(sizeof(int) * n), m = 0, k = ((s % n) + n) % n;
    for (int i = 0; i < n; i++) {
        if (fn[k].e < 0) { free(e); free(al); return 0; }
        e[m] = fn[k].e; al[m] = rev ? fn[k].pos : !fn[k].pos; m++;
        if (t >= 0 && k == t) break;
        k = rev ? (k + 1) % n : (k + n - 1) % n;
    }
    int ok = make_chain(b, e, al, m, c); free(e); free(al); return ok;
}
static void rebuild(void) {
    /* the live chains are cchains[sld_base ..]; their curves are F.ents[gen_from ..] when nothing was added since */
    for (int i = sld_base; i < ncchains; i++) cam_chain_free(&cchains[i]);
    ncchains = sld_base < ncchains ? sld_base : ncchains;
    if (gen_from >= 0 && gen_from <= F.nents) F.nents = gen_from;
    gen_from = F.nents; lastc0 = lastc1 = -1;
    for (int i = 0; i < nit && ncchains < 256; i++) {
        SItem *s = &it[i]; int c0 = ncchains;
        if (s->kind == 2) { if (make_chain(s->b, s->e, s->al, s->ne, &cchains[ncchains])) ncchains++; }
        else if (s->kind == 8) { if (loop_chain(s->b, s->L, s->s, s->t, s->rev, 0, &cchains[ncchains])) ncchains++; }
        else if (s->kind == 4) {                           /* ExtractToolPChainInput: PK_FACE_ask_loops, every loop */
            const BTopo *T = topo(s->b);
            for (int l = 0; T && l < T->face[s->f].nl && ncchains < 256; l++) {
                int L = T->face[s->f].l0 + l, n = T->loop[L].nf;
                if (loop_chain(s->b, L, s->rev && n > 1 ? 1 : 0, -1, s->rev, 1, &cchains[ncchains])) ncchains++;
            }
        }
        if (i == nit - 1) { lastc0 = c0; lastc1 = ncchains; }
    }
    overlay_dirty = 1;
}
static void item_free(SItem *s) { free(s->e); free(s->al); s->e = s->al = NULL; s->ne = 0; }
static SItem *item_add(SItem s) {
    if (nit == itcap) { itcap = itcap ? itcap * 2 : 32; it = realloc(it, sizeof(SItem) * itcap); }
    it[nit] = s; return &it[nit++];
}
static void item_del(int i) { item_free(&it[i]); memmove(it + i, it + i + 1, sizeof(SItem) * (nit - i - 1)); nit--; }
static void items_copy(SItem **dst, int *nd, const SItem *src, int ns) {
    for (int i = 0; i < *nd; i++) item_free(&(*dst)[i]);
    free(*dst); *dst = ns ? malloc(sizeof(SItem) * ns) : NULL; *nd = ns;
    for (int i = 0; i < ns; i++) { (*dst)[i] = src[i];
        if (src[i].ne) { (*dst)[i].e = malloc(sizeof(int) * src[i].ne); (*dst)[i].al = malloc(sizeof(int) * src[i].ne);
            memcpy((*dst)[i].e, src[i].e, sizeof(int) * src[i].ne); memcpy((*dst)[i].al, src[i].al, sizeof(int) * src[i].ne); } }
}
static void clear_items(void) { for (int i = 0; i < nit; i++) item_free(&it[i]); nit = 0; last_kind = 0; }

/* ---------------------------------------------------------------------------------------------- picking */
/* FUN_1800c2da0 -> sld_select(cursor, mask, hits, 1): the solid pick mask 6 (edges + faces) when the Edges button is
   checked else 4, faces dropped when Faces is not checked; edges first (front to back), then faces (the ray's front face).
   Window coordinates; returns 2 edge / 4 face / 0 none */
static int pick(float mx, float my, int mask, int body, int *b, int *idx, double p[3]) {
    float sx0 = mx - vp_x, sy0 = my - vp_y;
    if (mask & 2) {
        int bo[64], eo[64]; double dep[64], cp[192];
        int n = sld_edge_pick(sx0, sy0, body, sld_b_back, bo, eo, dep, cp, 64);
        if (n > 0) { *b = bo[0]; *idx = eo[0]; memcpy(p, cp, sizeof(double) * 3); return 2; }
    }
    if (!(mask & 4)) return 0;
    double vx, vy, o[3], o2[3], d[3]; view_px_to_view(sx0, sy0, &vx, &vy);
    view_to_world3(vx, vy, 1e6, o); view_to_world3(vx, vy, 0, o2);
    for (int k = 0; k < 3; k++) d[k] = o2[k] - o[k];
    int nsets = ac_solid_sets(), bb = -1, bf = -1; double bt = sld_b_back ? -1e300 : 1e300;
    for (int s = 0; s < nsets; s++) {
        if (body >= 0 && s != body) continue;
        const BTopo *T = topo(s); if (!T) continue;
        for (int f = 0; f < T->nface; f++) for (int k = 0; k < T->face[f].ntri; k++) {
            const float *A = T->face[f].tri + 9 * k, *B = A + 3, *C = A + 6;
            double e1[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, e2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
            double q[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
            double det = e1[0] * q[0] + e1[1] * q[1] + e1[2] * q[2]; if (fabs(det) < 1e-18) continue;
            double sv[3] = { o[0] - A[0], o[1] - A[1], o[2] - A[2] }, u = (sv[0] * q[0] + sv[1] * q[1] + sv[2] * q[2]) / det;
            if (u < 0 || u > 1) continue;
            double r[3] = { sv[1] * e1[2] - sv[2] * e1[1], sv[2] * e1[0] - sv[0] * e1[2], sv[0] * e1[1] - sv[1] * e1[0] };
            double v = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / det; if (v < 0 || u + v > 1) continue;
            double tt = (e2[0] * r[0] + e2[1] * r[1] + e2[2] * r[2]) / det;
            if (tt < 0) continue;
            double hp[3] = { o[0] + tt * d[0], o[1] + tt * d[1], o[2] + tt * d[2] };
            if (!sld_pos_visible(hp[0], hp[1], hp[2])) continue;   /* IsPosVisible(hit, 3) */
            if (sld_b_back ? tt > bt : tt < bt) { bt = tt; bb = s; bf = f; memcpy(p, hp, sizeof hp); }
        }
    }
    if (bb < 0) return 0;
    *b = bb; *idx = bf; return 4;
}
/* the pick's travel along edge e (FUN_1800a97c0: PK_EDGE_ask_geometry, the pick point compared with the two ends - on a
   closed edge the points 1/16 of the interval in from each end): along = the pick is nearer the start, so the chain runs
   from the end nearest the pick (formats/chaining.md) */
static int pick_along(int b, int e, const double *p) {
    const BEdge *EB; ac_solid_edges(b, &EB); const BEdge *E = &EB[e];
    const double *S = E->pts, *T = E->pts + 3 * (E->np - 1);
    double ds = hypot(hypot(S[0] - T[0], S[1] - T[1]), S[2] - T[2]);
    double a[3], z[3]; memcpy(a, S, sizeof a); memcpy(z, T, sizeof z);
    if (ds < cfg_sys_tol && E->np > 2) {                  /* closed: 1/16 of the polyline length in from each end */
        double L = 0; for (int k = 0; k + 1 < E->np; k++) L += hypot(hypot(E->pts[3 * k + 3] - E->pts[3 * k], E->pts[3 * k + 4] - E->pts[3 * k + 1]), E->pts[3 * k + 5] - E->pts[3 * k + 2]);
        for (int w = 0; w < 2; w++) { double want = w ? L * 15 / 16 : L / 16, acc = 0; double *o = w ? z : a;
            for (int k = 0; k + 1 < E->np; k++) { const double *A = E->pts + 3 * k, *B = A + 3; double l = hypot(hypot(B[0] - A[0], B[1] - A[1]), B[2] - A[2]);
                if (acc + l >= want && l > 0) { double u = (want - acc) / l; for (int j = 0; j < 3; j++) o[j] = A[j] + u * (B[j] - A[j]); break; } acc += l; } }
    }
    double d0 = hypot(hypot(p[0] - a[0], p[1] - a[1]), p[2] - a[2]), d1 = hypot(hypot(p[0] - z[0], p[1] - z[1]), p[2] - z[2]);
    return !(d1 < d0);
}
static void edge_end(int b, int e, int along, int at_end, double *q) {    /* the travel start (0) / end (1) point */
    const BEdge *EB; ac_solid_edges(b, &EB); const BEdge *E = &EB[e];
    int last = (at_end != 0) == (along != 0); memcpy(q, last ? E->pts + 3 * (E->np - 1) : E->pts, sizeof(double) * 3);
}
static double d3(const double *a, const double *b) { return hypot(hypot(a[0] - b[0], a[1] - b[1]), a[2] - b[2]); }
/* |face normal . the active Tplane's Z axis| (sld_get_face_normal_at_pt; MastercamPlanes::GetActivePlane(1 = Tplane:
   R8D = 1 at 0x1800aa0a9, decomp/mcgeomsld_pickloop_activeplane.txt) +0xa8 = the matrix's third row). The active
   Tplane of this program is always Top (the status bar's TPLANE; switching the active planes is NOT PORTED):
   cui_active_tplane() */
int cui_active_tplane(void) { return 1; }
static int nrm_dot(const BTopo *T, int fin, double *dot) {
    int L = fin_loop(T, fin); if (L < 0) return 0;
    const TFace *f = &T->face[T->loop[L].face]; const double *z = cam_plane_matrix(cui_active_tplane()) + 6;
    *dot = f->has_nrm ? fabs(f->nrm[0] * z[0] + f->nrm[1] * z[1] + f->nrm[2] * z[2]) : 0; return 1;
}
/* the loop pick finished with fin `fin` (the chosen face's fin of the picked edge): SLD_LOOP_INP +5 (rev) = travel along
   the fin (the edge travelled from the end nearest the pick, formats/chaining.md); a loop pick (not partial) with rev set
   moves the stored start one fin back (FUN_1800c14b0(inp, 0, 0, 0)) - so the picked edge comes first either way */
static SItem loop_item(int b, int fin, int N, int partial) {
    const BTopo *T = topo(b); SItem s; memset(&s, 0, sizeof s); s.kind = 8; s.b = b; s.t = -1;
    s.L = fin_loop(T, fin); s.s = fin - T->loop[s.L].f0;
    int along_edge = !N; s.rev = along_edge == (T->fin[fin].pos != 0);
    if (s.rev && !partial) s.s = (s.s + T->loop[s.L].nf - 1) % T->loop[s.L].nf;
    return s;
}
static void finish_loop(SItem s, int partial) {
    if (partial) { part_wait = 1; part_it = s; }
    else { item_add(s); last_kind = 8; rebuild(); }
}
/* FUN_1800a97c0 (sld_array_pick_loop_add) for a loop pick on edge e: the two faces of the edge (PK_EDGE_ask_faces); with
   SolidChainingUseFace off (the 3D radio) the default is the face whose normal is nearer the active plane's normal
   (larger |dot|, the first face on a tie), then the Pick Reference Face dialog always; with it on (Face) the last
   reference face is taken when the edge lies on it, else the dialog (default: the second face). The faces are taken in
   the edge's fin order here: first = the face of its positive fin (formats/chaining.md) */
static void loop_pick(int b, int e, const double *p, int partial) {
    const BTopo *T = topo(b); if (!T || e >= nefin[b]) return;
    int f0 = efin[b][2 * e], f1 = efin[b][2 * e + 1];
    if (f0 >= 0 && f1 >= 0 && !T->fin[f0].pos && T->fin[f1].pos) { int x = f0; f0 = f1; f1 = x; }
    int N = !pick_along(b, e, p);
    if (f0 < 0) return;
    if (f1 < 0) { finish_loop(loop_item(b, f0, N, partial), partial); return; }   /* one face */
    int ix;
    if (!sld_use_face) { double a0 = 0, a1 = 0; nrm_dot(T, f0, &a0); nrm_dot(T, f1, &a1); ix = a1 > a0; }
    else {
        int fa = T->loop[fin_loop(T, f0)].face, fb = T->loop[fin_loop(T, f1)].face;
        if (sld_ref_b == b && sld_ref_f == fa) { finish_loop(loop_item(b, f0, N, partial), partial); return; }
        if (sld_ref_b == b && sld_ref_f == fb) { finish_loop(loop_item(b, f1, N, partial), partial); return; }
        ix = 1;
    }
    ref_open = 1; ref_fin[0] = f0; ref_fin[1] = f1; ref_ix = ix; ref_N = N; ref_mode = partial;
    ref_it = loop_item(b, ref_fin[ix], N, 1); ref_it.b = b;
    overlay_dirty = 1; sld_prompt();
}
void sld_ref_other(void) {                         /* 273 Other face (FUN_18005d690): the other face's loop highlighted */
    if (!ref_open) return;
    ref_ix = !ref_ix; ref_it = loop_item(ref_it.b, ref_fin[ref_ix], ref_N, 1); overlay_dirty = 1;
}
void sld_ref_ok(void) {                            /* OK (or the get_event loop ending): the face is the reference face */
    if (!ref_open) return;
    ref_open = 0; const BTopo *T = topo(ref_it.b);
    sld_ref_b = ref_it.b; sld_ref_f = T->loop[fin_loop(T, ref_fin[ref_ix])].face;   /* DAT_1803e80e4 */
    finish_loop(loop_item(ref_it.b, ref_fin[ref_ix], ref_N, ref_mode), ref_mode);
    overlay_dirty = 1; sld_prompt();
}
int sld_ref_pending(void) { return ref_open; }

/* a click in the view during solid selection (FUN_1800c2da0 -> FUN_1800a97c0 / sld_array_pick_loops) */
int sld_click(float mx, float my) {
    if (!sld_mode) return 0;
    if (ref_open) return 1;                            /* the reference face dialog is up: the picks wait */
    double p[3]; int b, ix;
    if (part_wait) {                                   /* FUN_1800c2af0(body, 2 = edges, 450): an edge of the same loop ends it */
        if (pick(mx, my, 2, part_it.b, &b, &ix, p) != 2) return 1;
        const BTopo *T = topo(b); int L = part_it.L, hit = -1;
        for (int k = 0; k < T->loop[L].nf; k++) if (T->fin[T->loop[L].f0 + k].e == ix) hit = k;   /* ps_edge_on_loop */
        if (hit < 0) return 1;
        part_it.t = hit; part_wait = 0; item_add(part_it); last_kind = 8; rebuild(); sld_prompt(); return 1;
    }
    int mask = (sld_b_edges ? 6 : 4) & (sld_b_faces ? 6 : ~4);
    int k = pick(mx, my, mask, -1, &b, &ix, p);
    if (!k) return 1;
    if (k == 4) {                                      /* a face: toggled (an already selected face is removed) */
        for (int i = nit - 1; i >= 0; i--) if (it[i].kind == 4 && it[i].b == b && it[i].f == ix) { item_del(i); last_kind = nit ? it[nit - 1].kind : 0; rebuild(); return 1; }
        SItem s; memset(&s, 0, sizeof s); s.kind = 4; s.b = b; s.f = ix; item_add(s); last_kind = 4; rebuild(); return 1;
    }
    if (sld_b_loops) {                                 /* loop (Loops checked) / partial loop (Partial checked) */
        for (int i = nit - 1; i >= 0; i--) if (it[i].kind == 8 && it[i].b == b) {   /* the loop whose stored start edge this is: unselected */
            const BTopo *T = topo(b); if (T->fin[T->loop[it[i].L].f0 + it[i].s].e == ix) { item_del(i); last_kind = nit ? it[nit - 1].kind : 0; rebuild(); return 1; } }
        loop_pick(b, ix, p, sld_b_partial); return 1;
    }
    /* an edge: one already selected is removed from its run */
    for (int i = nit - 1; i >= 0; i--) if (it[i].kind == 2 && it[i].b == b) for (int j = 0; j < it[i].ne; j++) if (it[i].e[j] == ix) {
        memmove(it[i].e + j, it[i].e + j + 1, sizeof(int) * (it[i].ne - j - 1)); memmove(it[i].al + j, it[i].al + j + 1, sizeof(int) * (it[i].ne - j - 1)); it[i].ne--;
        if (!it[i].ne) { item_del(i); sld_finish_contig = 1; }
        last_kind = nit ? it[nit - 1].kind : 0; rebuild(); return 1;
    }
    int al = pick_along(b, ix, p);
    if (last_kind == 2 && nit && it[nit - 1].kind == 2 && sld_allow_contig) {   /* Linked edges: FUN_1800a2d00 */
        SItem *g = &it[nit - 1]; double pe[3], ns[3], ne[3];
        edge_end(g->b, g->e[g->ne - 1], g->al[g->ne - 1], 1, pe); edge_end(b, ix, al, 0, ns); edge_end(b, ix, al, 1, ne);
        int con = g->b == b && (d3(ns, pe) <= cfg_chain_tol || d3(ne, pe) <= cfg_chain_tol);
        if (con) {
            if (d3(ns, pe) > cfg_chain_tol) al = !al;  /* it touches with its far end: turned */
            g->e = realloc(g->e, sizeof(int) * (g->ne + 1)); g->al = realloc(g->al, sizeof(int) * (g->ne + 1));
            g->e[g->ne] = ix; g->al[g->ne] = al; g->ne++;
            sld_finish_contig = 0; sld_contig_wait = 0; rebuild(); sld_prompt(); return 1;
        }
        if (!sld_finish_contig) { sld_contig_wait = 1; sld_prompt(); return 1; }   /* FUN_18009b2c0(1): prompt 101, not taken */
    }
    SItem s; memset(&s, 0, sizeof s); s.kind = 2; s.b = b; s.ne = 1; s.e = malloc(sizeof(int)); s.al = malloc(sizeof(int)); s.e[0] = ix; s.al[0] = al;
    item_add(s); last_kind = 2;
    if (sld_allow_contig && sld_finish_contig) sld_finish_contig = 0;
    sld_contig_wait = 0; rebuild(); sld_prompt(); return 1;
}
/* the solid hover highlight while the solid pick mask is set (FUN_1804df930 -> sld_identify_from_cursor; FUN_180098b20:
   an edge is drawn in the select colour; a face's look is the GAF renderer's AutoHighlight - NOT DECODED, nothing) */
int sld_hover_update(float mx, float my) {
    int b = -1, e = -1;
    if (sld_mode && !ref_open) {
        double p[3]; int ix, bb;
        int mask = part_wait ? 2 : (sld_b_edges ? 6 : 4) & (sld_b_faces ? 6 : ~4);
        if (pick(mx, my, mask, part_wait ? part_it.b : -1, &bb, &ix, p) == 2) { b = bb; e = ix; }
    }
    if (b == sld_hover_b && e == sld_hover_e) return 0;
    sld_hover_b = b; sld_hover_e = e; overlay_dirty = 1; return 1;
}

/* ---------------------------------------------------------------------------------------------- mode changes / commands */
/* FUN_1801edde0 (351): the dialog re-created from 6518, the solid selection starts (sld_init_solid_chains_selection ran
   before chain_it: empty arrays) */
void sld_enter(void) {
    if (sld_mode) return;
    sld_mode = 1; sld_last_mode = 1; sld_base = ncchains; gen_from = -1; clear_items(); part_wait = 0; ref_open = 0;
    sld_dlg_refresh(); sld_prompt(); overlay_dirty = 1;
}
/* OnSolidsSelectEnd (the Wireframe button FUN_1801ee2b0, OK FUN_1801ed940): the selection ends - sld_array_pick_loops
   returns 1, FUN_18009fb70 keeps it for Last, sld_chains_to_new_tp_ops makes the chains */
void sld_commit(void) {
    if (!sld_mode) return;
    if (ref_open) sld_ref_ok();
    part_wait = 0;
    if (nit) items_copy(&last_it, &nlast_it, it, nit);
    for (int i = sld_base; i < ncchains; i++) if (cchains[i].sld == 2) cchains[i].sld = 1;
    clear_items(); gen_from = -1; lastc0 = lastc1 = -1; sld_hover_b = sld_hover_e = -1; overlay_dirty = 1;
}
void sld_leave(void) { sld_commit(); sld_mode = 0; sld_last_mode = 0; }   /* FUN_1801ee2b0: DAT_1813c4bb2 = 0 */
/* a new chaining (chain_dlg_init): run_chain_dlg's +0x17f8 = DAT_1813c4bb2 && allow_solid_chains && solids visible */
void sld_reset(void) {
    clear_items(); part_wait = 0; ref_open = 0; gen_from = -1; lastc0 = lastc1 = -1; sld_hover_b = sld_hover_e = -1;
    sld_base = ncchains; sld_mode = sld_last_mode && sld_allowed();
    if (sld_mode) sld_dlg_refresh();
}
/* Cancel (FUN_1801ec5e0): OnSolidsSelectClear + OnSolidsSelectCancel */
void sld_cancel(void) { clear_items(); part_wait = 0; ref_open = 0; gen_from = -1; sld_mode = 0; }
/* Esc: the reference face dialog's get_event loop ends (as OK); the partial loop's second pick returns 0 (cancelled);
   else the pick returns command 2 (sld_array_pick_loops returns 0: FUN_1801df360 -2 -> run_chain_dlg turns the Solids
   mode off and posts 537) - the selection is dropped and the dialog goes back to Wireframe. 1 = taken */
int sld_escape(void) {
    if (!sld_mode) return 0;
    if (ref_open) { sld_ref_ok(); return 1; }
    if (part_wait) { part_wait = 0; sld_prompt(); overlay_dirty = 1; return 1; }
    clear_items(); rebuild(); gen_from = -1; sld_mode = 0; sld_last_mode = 0; chain_prompt_upd(); return 1;
}
int sld_nitems(void) { return nit; }
void sld_clear(void) { if (part_wait) return; clear_items(); rebuild(); }   /* OnSolidsSelectClear (Unselect All's Yes, command 4) */

/* the CChainDlg handlers in Solids mode (message map @0x180d286c0; decomp/mccore_chain_range.c) */
int sld_act(int id) {
    if (!sld_mode) return 0;
    if (ref_open && id != 1 && id != 537) return 1;    /* the reference face dialog is up */
    switch (id) {
    case 351: return 1;                                /* FUN_1801edde0: already in Solids mode */
    case 3041: sld_use_face = 1; return 1;             /* FUN_1801ecf20: SetSolidChainingUseFace(1) */
    case 1499: sld_use_face = 0; return 1;             /* FUN_1801ec560 in Solids mode: SetSolidChainingUseFace(0) */
    case 2953: case 2956: {                            /* Edge FUN_1801eca80 / Linked edges FUN_1801ed740 */
        if (part_wait) return 1;                       /* GetSldPartialInProgress: the click is undone */
        int c = id == 2953 ? !sd_edge : !sd_linked;    /* the check box's state after the click */
        if (id == 2956) sd_linked = c;
        if (c) { sld_b_partial = 1; on_partial(); sld_b_loops = 1; on_loop(); sld_allow_contig = id == 2956; }
        if (!c || !sld_b_edges) on_edge();
        sld_dlg_refresh(); break; }
    case 2955: {                                       /* Loop FUN_1801ed830 */
        if (part_wait) return 1;
        int c = !sd_loop;
        if (c) { sld_b_partial = 1; on_partial(); }
        if (!c || !sld_b_loops) on_loop();
        if (!sld_b_loops) { sld_b_edges = 1; on_edge(); }
        sld_dlg_refresh(); break; }
    case 2957:                                         /* Partial loop FUN_1801edac0 */
        if (part_wait) return 1;
        on_partial(); if (!sld_b_partial) { sld_b_edges = 1; on_edge(); }
        sld_dlg_refresh(); break;
    case 2954: if (part_wait) return 1; on_face(); sld_dlg_refresh(); break;     /* Face FUN_1801ece90 */
    case 2959: if (part_wait) return 1; sld_b_back = !sld_b_back; sld_dlg_refresh(); break;   /* From back FUN_1801ecf30 -> OnSolidsSelectBack */
    case 12321: sld_finish_contig = 1; sld_contig_wait = 0; break;   /* End chain FUN_1801ecb70: SetSldFinishContiguousEdges(true) */
    case 25381:                                        /* Reverse FUN_1801edc50 -> OnSolidsSelectReverse (command 1, FUN_1800aaae0) */
        if (part_wait || !nit) break;
        if (last_kind == 2 && it[nit - 1].kind == 2) { SItem *g = &it[nit - 1];   /* the last run: order and directions reversed */
            for (int a = 0, z = g->ne - 1; a < z; a++, z--) { int t = g->e[a]; g->e[a] = g->e[z]; g->e[z] = t; t = g->al[a]; g->al[a] = g->al[z]; g->al[z] = t; }
            for (int a = 0; a < g->ne; a++) g->al[a] = !g->al[a]; }
        else if (last_kind == 4 && it[nit - 1].kind == 4) it[nit - 1].rev = !it[nit - 1].rev;   /* the face's flag */
        else if (last_kind == 8 && it[nit - 1].kind == 8) it[nit - 1].rev = !it[nit - 1].rev;   /* SLD_LOOP_INP +5 */
        rebuild(); break;
    case 542: case 561:                                /* Start Back / Forward FUN_1801ede80 / FUN_1801edf60 -> commands 6 / 5:
                                                          FUN_1800aa5f0 / FUN_1800aa6c0 -> FUN_1800c14b0(inp, 0 / 1): a loop only */
        if (part_wait || !nit || last_kind != 8 || it[nit - 1].kind != 8 || it[nit - 1].t >= 0) break;
        { SItem *s = &it[nit - 1]; const BTopo *T = topo(s->b); int n = T->loop[s->L].nf, fwd = id == 561;
          s->s = ((s->rev ? (fwd ? s->s + 1 : s->s - 1) : (fwd ? s->s - 1 : s->s + 1)) % n + n) % n; }
        rebuild(); break;
    case 541:                                          /* Unselect FUN_1801ee030 -> OnSolidsSelectRemove (command 7, FUN_1800aa790) */
        if (part_wait || !nit) break;
        { SItem *s = &it[nit - 1];
          if (s->kind == 2 && s->ne > 1) s->ne--; else item_del(nit - 1); }
        last_kind = nit ? it[nit - 1].kind : 0; rebuild(); break;
    case 525:                                          /* Last FUN_1801ed6b0 -> OnSolidsSelectLast (command 3, FUN_18009f930 / f960 / f110) */
        if (part_wait) break;
        if (!nlast_it) { cui_message("apprenticeCAM", "There are no previous selections."); break; }   /* mcgeomsldres 669 */
        { int ok = 1; for (int i = 0; i < nlast_it; i++) { const SItem *s = &last_it[i]; const BTopo *T = topo(s->b);
              if (!T || (s->kind == 4 && s->f >= T->nface) || (s->kind == 8 && s->L >= T->nloop)) ok = 0;
              for (int j = 0; j < s->ne; j++) if (s->b >= ntp || s->e[j] >= nefin[s->b]) ok = 0; }
          if (!ok) { cui_message("apprenticeCAM", "The last selection is no longer valid."); break; }   /* mcgeomsldres 274 */
          SItem *cp = NULL; int nc = 0; items_copy(&cp, &nc, last_it, nlast_it);
          for (int i = 0; i < nc; i++) item_add(cp[i]);
          free(cp); last_kind = nit ? it[nit - 1].kind : 0; rebuild(); }
        break;
    case 10204:                                        /* Unselect All FUN_1801ee0f0: MCMessageBox 5007 / 10206, Yes -> OnSolidsSelectClear */
        if (part_wait) break;
        if (nit && cui_hooks.chain_ask_unselect_all) cui_hooks.chain_ask_unselect_all();
        break;
    case 9: break;
    default: return 0;                                 /* 537 Wireframe, 1 OK: cui_chain.c */
    }
    sld_prompt(); overlay_dirty = 1; return 1;
}

/* the prompt: FUN_1800c2da0 mcgeomsldres 441 "Solid Toolpath Chain:\nSelect faces, edges, and/or loops", or 101 "Click
   End chain, or continue from last end point." while FUN_180097c80 (DAT_1803e8114) is set; the partial loop's second
   pick 450 "Select edge for end-of-loop" */
void sld_prompt(void) {
    if (!sld_mode) return;
    if (part_wait) snprintf(cam_msg, sizeof cam_msg, "Select edge for end-of-loop");
    else if (sld_contig_wait) snprintf(cam_msg, sizeof cam_msg, "Click End chain, or continue from last end point.");
    else snprintf(cam_msg, sizeof cam_msg, "Solid Toolpath Chain:\nSelect faces, edges, and/or loops");
}

/* ---------------------------------------------------------------------------------------------- drawing */
static void ov_bedge(float **pv, int *pn, int *pcap, int b, int e, const float *rgb) {
    const BEdge *EB; int ne = ac_solid_edges(b, &EB); if (e < 0 || e >= ne) return;
    const BEdge *E = &EB[e];
    for (int k = 0; k + 1 < E->np; k++) {
        V3 a = { E->pts[3 * k], E->pts[3 * k + 1], E->pts[3 * k + 2] }, c = { E->pts[3 * k + 3], E->pts[3 * k + 4], E->pts[3 * k + 5] };
        ov_pt(pv, pn, pcap, a, rgb[0], rgb[1], rgb[2]); ov_pt(pv, pn, pcap, c, rgb[0], rgb[1], rgb[2]);
    }
}
static void ov_loop(float **pv, int *pn, int *pcap, int b, const SItem *s, const float *rgb) {   /* sld_hilight_loop */
    const BTopo *T = topo(b); if (!T || s->L < 0) return;
    int n = T->loop[s->L].nf, k = s->rev && s->t < 0 ? (s->s + 1) % n : s->s;
    for (int i = 0; i < n; i++) {
        ov_bedge(pv, pn, pcap, b, T->fin[T->loop[s->L].f0 + k].e, rgb);
        if (s->t >= 0 && k == s->t) break;
        k = s->rev ? (k + 1) % n : (k + n - 1) % n;
    }
}
/* the selected solid topology (ps_hilight_edges -> ps_redraw_hilights -> FUN_180036fc0 with colour -1): edges in
   sldEdge_select_color (config 5034); a face: shaded (render_cfg 3 / 4) filled in the select colour (FUN_1800bc970 -
   NOT PORTED: the overlay draws lines), not shaded its topology as wire in sldFace_select_color (config 5035); the
   edge under the cursor in the select colour (5033); the pending reference face's loop and the partial loop's first
   loop as loops (sld_hilight_loop -> ps_hilight_edges: 5034) */
int cfg_sld_edge_color = 9, cfg_sld_face_color = 12;    /* config 5034 / 5035 (mccore FUN_1801d3400: 0x13aa / 0x13ab) */
void sld_overlay(float **pv, int *pn, int *pcap) {
    if (!sld_mode) return;
    float ec[3], fc[3], hc[3]; mc_rgb(cfg_sld_edge_color, ec); mc_rgb(cfg_sld_face_color, fc); mc_rgb(cfg_select_color, hc);
    for (int i = 0; i < nit; i++) {
        const SItem *s = &it[i];
        if (s->kind == 2) for (int j = 0; j < s->ne; j++) ov_bedge(pv, pn, pcap, s->b, s->e[j], ec);
        else if (s->kind == 8) ov_loop(pv, pn, pcap, s->b, s, ec);
        else if (s->kind == 4 && solid_mode != 1) {
            const BTopo *T = topo(s->b);
            for (int l = 0; T && l < T->face[s->f].nl; l++) { const TLoop *L = &T->loop[T->face[s->f].l0 + l];
                for (int k = 0; k < L->nf; k++) ov_bedge(pv, pn, pcap, s->b, T->fin[L->f0 + k].e, fc); }
        }
    }
    if (ref_open) { SItem s = ref_it; ov_loop(pv, pn, pcap, s.b, &s, ec); }
    if (part_wait) { SItem s = part_it; s.t = -1; ov_loop(pv, pn, pcap, s.b, &s, ec); }
    if (sld_hover_b >= 0) ov_bedge(pv, pn, pcap, sld_hover_b, sld_hover_e, hc);
}
/* chain_arrows: the solid selection shows the last pick's arrows only (EraseArrows(true) before every
   sld_show_loop_arrow / SldShowEdgeLoopArrow / sld_show_face_loop_arrow); chains of an ended solid selection get none
   (they are not CHAINs of the dialog: sld_chains_to_toolp_op makes them after chain_it) */
int sld_chain_arrows(int ci) {
    if (ci < 0 || ci >= ncchains || !cchains[ci].sld) return 1;
    return sld_mode && ci >= lastc0 && ci < lastc1;
}

/* ---------------------------------------------------------------------------------------------- tests */
void sld_dump(void) {
    if (!sld_mode && !nit && !sld_last_mode) return;
    printf("    cd sld mode %d last %d btn e%d f%d l%d p%d b%d contig %d/%d/%d face %d dlg e%d f%d l%d p%d b%d k%d items %d kind %d ref %d part %d base %d hov %d/%d\n",
           sld_mode, sld_last_mode, sld_b_edges, sld_b_faces, sld_b_loops, sld_b_partial, sld_b_back, sld_allow_contig, sld_finish_contig, sld_contig_wait,
           sld_use_face, sd_edge, sd_face, sd_loop, sd_part, sd_back, sd_linked, nit, last_kind, ref_open ? ref_ix : -1, part_wait, sld_base, sld_hover_b, sld_hover_e);
    for (int i = 0; i < nit; i++) {
        const SItem *s = &it[i];
        printf("    cd sld item %d: kind %d body %d", i, s->kind, s->b);
        if (s->kind == 8) printf(" loop %d start %d end %d rev %d", s->L, s->s, s->t, s->rev);
        if (s->kind == 4) printf(" face %d rev %d", s->f, s->rev);
        if (s->kind == 2) { printf(" edges"); for (int j = 0; j < s->ne; j++) printf(" %d%s", s->e[j], s->al[j] ? "" : "r"); }
        printf("\n");
    }
}
/* sldclick:X,Y,Z (ours): a click at the screen point of world X,Y,Z in the view; sldrefother / sldrefok: the Pick
   Reference Face dialog's buttons; sldesc: Esc; slddump. 1 = handled */
int sld_script(const char *st, const char *arg) {
    if (!strcmp(st, "sldclick") && arg) {
        double x = 0, y = 0, z = 0; sscanf(arg, "%lf,%lf,%lf", &x, &y, &z); float sx, sy;
        if (cui_hooks.view_port) cui_hooks.view_port();
        if (scene_to_screen(vp_w, vp_h, x, y, z, &sx, &sy)) { sld_click(sx + vp_x, sy + vp_y); chain_prompt_upd(); }
        return 1;
    }
    if (!strcmp(st, "sldrefother")) { sld_ref_other(); return 1; }
    if (!strcmp(st, "sldrefok")) { sld_ref_ok(); return 1; }
    if (!strcmp(st, "sldesc")) { sld_escape(); return 1; }
    if (!strcmp(st, "slddump")) { sld_dump(); return 1; }
    if (!strcmp(st, "sldok")) { printf("    cd sldok %d (cam %d)\n", sld_allowed(), cam_mode); return 1; }   /* the Solids button live */
    return 0;
}
/* the part solid (F.solids index) whose face is under the cursor - From Solid's body pick (get_event event 2 / 0x1000), -1 none */
/* the part solid and its face (XT face order: T->face) under the cursor; 1 found. hp: the hit point (the ray's front face, the same pick as sld_pick_part_solid) */
int sld_pick_part_face(float mx, float my, int *solid, int *face, double hp[3]) {
    int b, ix; double p[3];
    if (pick(mx, my, 4, -1, &b, &ix, p) != 4) return 0;
    int nb = ac_solid_nbodies();
    if (b < nb || b - nb >= F.nsolids) return b < nb ? 2 : 0;   /* 2: a solid of this session (solid25 Body), not an OCCT / XT body */
    *solid = b - nb; *face = ix; memcpy(hp, p, sizeof p);
    return 1;
}
/* tests (ours, cui_wfmisc.c lnfacechk): the mesh faces of a part solid: their number; a face's largest triangle's centre */
int sld_part_nface(int si) { const BTopo *T = topo(ac_solid_nbodies() + si); return T ? T->nface : 0; }
int sld_part_face_pt(int si, int f, float c[3]) {
    const BTopo *T = topo(ac_solid_nbodies() + si); if (!T || f < 0 || f >= T->nface || !T->face[f].ntri) return 0;
    double best = -1;
    for (int k = 0; k < T->face[f].ntri; k++) { const float *A = T->face[f].tri + 9 * k, *B = A + 3, *C = A + 6; double e1[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, e2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
        double nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2], nz = e1[0] * e2[1] - e1[1] * e2[0], a = nx * nx + ny * ny + nz * nz;
        if (a > best) { best = a; for (int q = 0; q < 3; q++) c[q] = (A[q] + B[q] + C[q]) / 3.f; } }
    return best >= 0;
}
int sld_pick_part_solid(float mx, float my) {
    int b, ix; double p[3];
    if (pick(mx, my, 4, -1, &b, &ix, p) != 4) return -1;
    int nb = ac_solid_nbodies();
    return b >= nb && b - nb < F.nsolids ? b - nb : -1;
}
