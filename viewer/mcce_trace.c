/* mcce_trace.c - ours, test tooling only (no engine rule here): the port's side of the mcce.dll oracle
 * (tools/peloader --dyn, tools/tests/mcce_dll_oracle.sh). #included by mcce_dyn.c; both hooks do nothing unless enabled.
 *
 * MCCE_DUMPIN=PREFIX   mcce_calc_dyn_pock writes its inputs to PREFIX.N (N = 0, 1, ... per call in this process): the
 *                      McceParm base fields by their CDynPockParm offset and the three boundary sets, doubles as %a, so the
 *                      original engine can be run on exactly the same input (tools/peloader/mcce.c reads this format).
 * MCCE_TRACE=FILE      one line per committed step (commit = FUN_180035990): the step entity, the measured widths w1 / w2
 *                      (M+0x120 of M1 / M2), engaged sweeps, flags after the commit, the material position, and a summary
 *                      of the material loops after it (C+0x410 and the bullnose material C+0x438); MCCE_TRACE_LEVEL=2 adds
 *                      one line per loop, 3 one line per entity. tools/peloader/mcce.c prints the same format from the
 *                      DLL's own data, so the two traces can be compared line by line (tools/mcce_trace_cmp.py).
 * MCCE_RECSIN=PREFIX   use the original engine's records instead of running the port (below). */

static _Thread_local int g_trace_inited;   /* _Thread_local like every engine static (TSan harness); trace from one engine thread */
static _Thread_local FILE *g_trace_f;
static _Thread_local int g_trace_lvl;
static _Thread_local long g_trace_n;
static _Thread_local int g_dumpin_n;

static void mtr_init(void) {
    if (g_trace_inited) return;
    g_trace_inited = 1;
    const char *f = getenv("MCCE_TRACE");
    if (f && *f) { g_trace_f = fopen(f, "w"); if (g_trace_f) setvbuf(g_trace_f, NULL, _IOFBF, 1 << 16); }
    const char *l = getenv("MCCE_TRACE_LEVEL");
    g_trace_lvl = l ? atoi(l) : 1;
}

/* exact signed area of a closed loop of lines and arcs (shoelace + circular segments) - same formula as tools/peloader/mcce.c */
static double mtr_area(const Sh *s) {
    double a = 0;
    for (int i = 0; i < s->n; i++) {
        const Ent *e = &s->e[i];
        if (e->id == 'L') a += e->p1.x * e->p2.y - e->p2.x * e->p1.y;
        else if (e->id == 'A') {
            double x1 = e->c.x + e->r * cos(e->sa), y1 = e->c.y + e->r * sin(e->sa);
            double x2 = e->c.x + e->r * cos(e->sa + e->sw), y2 = e->c.y + e->r * sin(e->sa + e->sw);
            a += x1 * y2 - x2 * y1 + e->r * e->r * (e->sw - sin(e->sw));
        }
    }
    return a * 0.5;
}
static int mtr_prec(void) { static int p = -1; if (p < 0) { const char *e = getenv("MCCE_TRACE_PREC"); p = e && *e ? atoi(e) : 6; } return p; }   /* ours: MCCE_TRACE_PREC=17 (both sides) */
static void mtr_ent(FILE *f, const char *tag, const Ent *e) {
    if (mtr_prec() != 6) {
        int q = mtr_prec();
        if (e->id == 'A') fprintf(f, "%s A %.*g %.*g %.*g %.*g c %.*g %.*g r %.*g sa %.*g sw %.*g", tag, q, e->c.x + e->r * cos(e->sa), q, e->c.y + e->r * sin(e->sa),
                                  q, e->c.x + e->r * cos(e->sa + e->sw), q, e->c.y + e->r * sin(e->sa + e->sw), q, e->c.x, q, e->c.y, q, e->r, q, e->sa, q, e->sw);
        else fprintf(f, "%s %c %.*g %.*g %.*g %.*g", tag, e->id ? e->id : '?', q, e->p1.x, q, e->p1.y, q, e->p2.x, q, e->p2.y);
        return;
    }
    if (e->id == 'A') {
        fprintf(f, "%s A %.6f %.6f %.6f %.6f c %.6f %.6f r %.6f sa %.6f sw %.6f", tag,
                e->c.x + e->r * cos(e->sa), e->c.y + e->r * sin(e->sa), e->c.x + e->r * cos(e->sa + e->sw), e->c.y + e->r * sin(e->sa + e->sw),
                e->c.x, e->c.y, e->r, e->sa, e->sw);
    } else fprintf(f, "%s %c %.6f %.6f %.6f %.6f", tag, e->id ? e->id : '?', e->p1.x, e->p1.y, e->p2.x, e->p2.y);
}
static void mtr_mat(FILE *f, const char *name, const ShL *l) {
    double tot = 0; long ne = 0;
    for (int i = 0; i < l->n; i++) { tot += mtr_area(l->s[i]); ne += l->s[i]->n; }
    fprintf(f, " %s %d %ld %.6f", name, l->n, ne, tot);
}
static void mtr_mat_detail(FILE *f, const char *name, const ShL *l) {
    for (int i = 0; i < l->n; i++) {
        const Sh *s = l->s[i];
        fprintf(f, "  %s %d dir %d isl %d n %d area %.6f box %.6f %.6f %.6f %.6f\n", name, i, s->dir, s->fl & 1, s->n, mtr_area(s),
                s->bb[0], s->bb[1], s->bb[2], s->bb[3]);
        if (getenv("MCCE_TRACE_SW")) fprintf(f, "  tag %d\n", s->tag);   /* MCCE_TRACE_SW: CSh +0x58 */
        if (g_trace_lvl >= 3) for (int k = 0; k < s->n; k++) { mtr_ent(f, "   e", &s->e[k]); fputc('\n', f); }
    }
}

/* MCCE_TRACE_RANGE=A,B (ours, oracle): the level >= 2 material detail only for commits A..B (both sides, same rule) */
static int mtr_in_range(long n) {
    static long a = -2, b = -1;
    if (a == -2) { const char *e = getenv("MCCE_TRACE_RANGE"); a = -1; if (e && *e) { a = atol(e); const char *c = strchr(e, ','); b = c ? atol(c + 1) : a; } }
    return a < 0 || (n >= a && n <= b);
}
/* called at the end of commit() (FUN_180035990) */
static void mcce_trace_commit(const Pock *C, const EnCut *E) {
    mtr_init();
    if (!g_trace_f) return;
    FILE *f = g_trace_f;
    fprintf(f, "C %ld", g_trace_n++);
    mtr_ent(f, "", &E->e);
    fprintf(f, " w %.6f %.6f sw %.6f %.6f fl %x %x pos %d %d %.6f %.6f %.6f |", E->m1.w, E->m2.w, E->m1.sweep, E->m2.sweep,
            E->m1.flags, E->m2.flags, C->mpos.sh, C->mpos.ent, C->mpos.t, C->mpos.pt.x, C->mpos.pt.y);
    mtr_mat(f, "mat", &C->mat);
    mtr_mat(f, "bull", &C->bull);
    if (mtr_prec() != 6) fprintf(f, " W %.17g %.17g S %.17g %.17g", E->m1.w, E->m2.w, E->m1.sweep, E->m2.sweep);   /* MCCE_TRACE_PREC */
    if (getenv("MCCE_TRACE_SW")) fprintf(f, " nrec %d", C->T.n);   /* MCCE_TRACE_SW: the record count (tools/peloader: CCetp +0x1c) */
    fputc('\n', f);
    if (g_trace_lvl >= 2 && mtr_in_range(g_trace_n - 1)) { mtr_mat_detail(f, "mat", &C->mat); mtr_mat_detail(f, "bull", &C->bull); }
}

/* level >= 4: every measurement (FUN_180030be0 = measure()), the "M" line of tools/peloader/mcce.c hook_measure */
static void mcce_trace_measure(const EnCut *E, int line) {
    mtr_init();
    if (!g_trace_f || g_trace_lvl < 4 || !mtr_in_range(g_trace_n)) return;   /* MCCE_TRACE_RANGE: the measures before commits A..B */
    fprintf(g_trace_f, "M");
    mtr_ent(g_trace_f, "", &E->e);
    fprintf(g_trace_f, " w %.6f %.6f sw %.6f %.6f fl %x %x", E->m1.w, E->m2.w, E->m1.sweep, E->m2.sweep, E->m1.flags, E->m2.flags);
    if (g_trace_lvl >= 5) fprintf(g_trace_f, " line %d", line);   /* the mcce_dyn.c call site */
    if (mtr_prec() != 6) fprintf(g_trace_f, " W %.17g %.17g S %.17g %.17g", E->m1.w, E->m2.w, E->m1.sweep, E->m2.sweep);
    fputc('\n', g_trace_f);
}
/* MCCE_TRACE_SW=1 (diagnostic, with MCCE_TRACE_LEVEL >= 4, inside MCCE_TRACE_RANGE): before each "M" line, the M1 swept region
   FUN_1800321e0 bit for bit (%a), its area M+0xc8 (FUN_180126630) and width x length; tools/peloader hook_measure prints the same */
static void mtr_ent_a(FILE *f, const Ent *e) {
    if (e->id == 'A') fprintf(f, "  A c %a %a r %a sa %a sw %a\n", e->c.x, e->c.y, e->r, e->sa, e->sw);
    else fprintf(f, "  %c %a %a %a %a\n", e->id ? e->id : '?', e->p1.x, e->p1.y, e->p2.x, e->p2.y);
}
static int mcce_trace_sw_on(void) { mtr_init(); static int on = -1; if (on < 0) on = getenv("MCCE_TRACE_SW") != NULL; return on && g_trace_f && g_trace_lvl >= 4 && mtr_in_range(g_trace_n); }
static void mcce_trace_tan(const char *k, int r, P2 v) { if (g_trace_lvl >= 5 && mcce_trace_sw_on()) fprintf(g_trace_f, "TAN %s %d %a %a\n", k, r, v.x, v.y); }
static void mcce_trace_pstep(const Sh *path, const Pos *a, const Pos *b, const Ent *out) {
    if (!mcce_trace_sw_on()) return;
    fprintf(g_trace_f, "PS %d %d %a %a %a | %d %d %a %a %a\n", 0, a->ent, a->t, a->pt.x, a->pt.y, 0, b->ent, b->t, b->pt.x, b->pt.y);
    mtr_ent_a(g_trace_f, &path->e[a->ent]); if (b->ent != a->ent) mtr_ent_a(g_trace_f, &path->e[b->ent]);
    mtr_ent_a(g_trace_f, out);
}
static void mcce_trace_leadin(const Ent *st, P2 X, const Sh *lead) {
    if (!mcce_trace_sw_on()) return;
    fprintf(g_trace_f, "LI X %a %a\n", X.x, X.y); mtr_ent_a(g_trace_f, st);
    for (int k = 0; k < lead->n; k++) mtr_ent_a(g_trace_f, &lead->e[k]);
}
static void mcce_trace_ev(const char *name) { if (mcce_trace_sw_on()) fprintf(g_trace_f, "LK %s\n", name); }   /* "LK" lines (tools/peloader ev_hooks) */
static void mcce_trace_loops(const ShL *lps, double removed) {
    mtr_init(); static int on = -1; if (on < 0) on = getenv("MCCE_TRACE_SW") != NULL;
    if (!g_trace_f || !on || g_trace_lvl < 4 || !mtr_in_range(g_trace_n)) return;
    fprintf(g_trace_f, "LP n %d removed %a\n", lps->n, removed);
    for (int i = 0; i < lps->n; i++) { fprintf(g_trace_f, " lp %d n %d\n", i, lps->s[i]->n); for (int k = 0; k < lps->s[i]->n; k++) mtr_ent_a(g_trace_f, &lps->s[i]->e[k]); }
}
static void mcce_trace_swept(const Sh *sw, double area, double wl) {
    mtr_init(); static int on = -1; if (on < 0) on = getenv("MCCE_TRACE_SW") != NULL;
    if (!g_trace_f || !on || g_trace_lvl < 4 || !mtr_in_range(g_trace_n)) return;
    fprintf(g_trace_f, "SW n %d area %a wl %a\n", sw->n, area, wl);
    for (int k = 0; k < sw->n; k++) mtr_ent_a(g_trace_f, &sw->e[k]);
}
/* MCCE_PLACEDBG=1 (diagnostic): FUN_18001efb0's inputs / output, as tools/peloader hook_place */
static void mcce_trace_place(double tol, double w, P2 A, P2 B, int r, P2 out) {
    mtr_init(); static int on = -1; if (on < 0) on = getenv("MCCE_PLACEDBG") != NULL;
    if (g_trace_f && on && mtr_in_range(g_trace_n)) fprintf(g_trace_f, "P %.17g %.17g A %.17g %.17g B %.17g %.17g -> %d %.17g %.17g\n", tol, w, A.x, A.y, B.x, B.y, r, out.x, out.y);
}
static void mcce_trace_trial(double ang, double len) { mtr_init(); if (g_trace_f && g_trace_lvl >= 5 && mtr_in_range(g_trace_n)) fprintf(g_trace_f, "T %.*g %.*g\n", mtr_prec() != 6 ? 17 : 9, ang, mtr_prec() != 6 ? 17 : 9, len); }
/* MCCE_TRACE_SW with level >= 5: the trial line's attach result after FUN_180017a10 ("TG" line: G+8 valid, +0xa8 sh, +0xac ent,
   +0xb0 t, lead count and entities in %a); tools/peloader mcce.c hook_trial the same */
static void mcce_trace_tg(const Gbap *G) {
    if (!(g_trace_f && g_trace_lvl >= 5 && mcce_trace_sw_on() && mtr_in_range(g_trace_n))) return;
    fprintf(g_trace_f, "TG %d %d %d %a n %d\n", G->valid, G->sh, G->ent, G->t, G->lead.n);
    for (int k = 0; k < G->lead.n; k++) mtr_ent_a(g_trace_f, &G->lead.e[k]);
}
/* MCCE_TRACE_SW with level >= 5: the attach's probe result ("AP"), the U-turn's crossings ("AUI") and result ("AU"); tools/peloader
   mcce.c hook_probe / hook_isx / hook_uturn the same */
static int mtr_l5(void) { return g_trace_f && g_trace_lvl >= 5 && mcce_trace_sw_on() && mtr_in_range(g_trace_n); }
static void mcce_trace_ap(int r) { if (mtr_l5()) fprintf(g_trace_f, "AP %d\n", r); }
static void mcce_trace_aui(const IsabL *I, const Sh *A) {
    if (!mtr_l5()) return;
    fprintf(g_trace_f, "AUI %d", I->n);
    for (int k = 0; k < I->n; k++) fprintf(g_trace_f, " | xt %a pt %a %a y %d %d %a", I->r[k].xt, I->r[k].pt.x, I->r[k].pt.y, I->r[k].ys, I->r[k].ye, I->r[k].yt);
    fputc('\n', g_trace_f);
    if (A && A->n) mtr_ent_a(g_trace_f, &A->e[0]);
}
static void mcce_trace_au(const Gbap *G) {
    if (!mtr_l5()) return;
    fprintf(g_trace_f, "AU %d n %d\n", G->valid, G->lead.n);
    for (int k = 0; k < G->lead.n; k++) mtr_ent_a(g_trace_f, &G->lead.e[k]);
}
static void mtr_bnds(FILE *f, int set, const McceBnd *b, int n) {
    for (int i = 0; i < n; i++) {
        if (b[i].ne > 0) {   /* the entity form (peloader mcce.c "B"): the bound's own lines / arcs */
            fprintf(f, "B %d %d %d %d\n", set, b[i].island, b[i].open, b[i].ne);
            for (int k = 0; k < b[i].ne; k++) { const double *v = b[i].ent + 5 * k;
                if (b[i].ety[k] == 'A') fprintf(f, "A %a %a %a %a %a\n", v[0], v[1], v[2], v[3], v[4]);
                else fprintf(f, "L %a %a %a %a\n", v[0], v[1], v[2], v[3]); }
            continue;
        }
        fprintf(f, "b %d %d %d %d\n", set, b[i].island, b[i].open, b[i].n);
        for (int k = 0; k < b[i].n; k++) fprintf(f, "%a %a\n", b[i].xy[2 * k], b[i].xy[2 * k + 1]);
    }
}
/* called at the top of mcce_calc_dyn_pock: the inputs, by CDynPockParm offset (mcce_dyn.h McceParm comments) */
static void mcce_trace_input(const McceParm *P, const McceBnd *s1, int n1, const McceBnd *s2, int n2, const McceBnd *s3, int n3) {
    static _Thread_local int ncall;
    mtr_init();
    if (g_trace_f) { fprintf(g_trace_f, "# call %d\n", ncall); g_trace_n = 0; }   /* the DLL trace of input .N starts at C 0 too */
    ncall++;
    const char *pre = getenv("MCCE_DUMPIN");
    if (!pre || !*pre) return;
    char path[4096]; snprintf(path, sizeof path, "%s.%d", pre, g_dumpin_n++);
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "MCCEIN 1\n");
    fprintf(f, "u32 0x08 %u\nu8 0x0c %d\nf64 0x10 %a\nf64 0x18 %a\nu8 0x20 %d\n", P->flags, P->start_sel, P->start_x, P->start_y, P->order_mode);
    const struct { int off; double v; } d[] = {
        { 0x28, P->R }, { 0x30, P->r }, { 0x38, P->eng }, { 0x40, P->min_rad }, { 0x48, P->micro_lift }, { 0x50, P->approach },
        { 0x58, P->sliver }, { 0x60, P->first_off }, { 0x68, P->plunge_ang }, { 0x70, P->pitch }, { 0x78, P->top },
        { 0x80, P->depth }, { 0x88, P->zclear }, { 0x90, P->helix_r }, { 0x98, P->troch_r }, { 0xa0, P->slot_add },
        { 0xa8, P->fp_red }, { 0xb0, P->f_retract }, { 0xb8, P->f_back }, { 0xc0, P->f_ramp }, { 0xc8, P->f_cut },
        { 0xd0, P->f_first }, { 0xd8, P->f_conv }, { 0xe0, P->gap }, { 0xe8, P->ct_t0 }, { 0xf0, P->ct_t1 }, { 0xf8, P->ct_t2 },
        { 0x108, P->ct_w }, { 0x110, P->chain_tol } };
    for (size_t i = 0; i < sizeof d / sizeof *d; i++) fprintf(f, "f64 %#x %a\n", d[i].off, d[i].v);
    fprintf(f, "i32 0x100 %d\n", P->ct_n);
    mtr_bnds(f, 1, s1, n1); mtr_bnds(f, 2, s2, n2); mtr_bnds(f, 3, s3, n3);
    fprintf(f, "end\n");
    fclose(f);
}

/* MCCE_RECSIN=PREFIX: mcce_calc_dyn_pock returns the records in PREFIX.N (N = call number) instead of running the port -
   the original engine's records (tools/peloader --dyn --raw), so the rest of cam_cli (glue, NC, oracle tools) runs on them.
   Lines "R id x1 y1 x2 y2 cx cy r sa sw z ce mf feed" (%a); z0 = the previous record's z, as in mcce_calc_dyn_pock. */
static _Thread_local int g_recsin_n;
static int mcce_trace_recs_in(McceRec **out, int *nout) {
    const char *pre = getenv("MCCE_RECSIN");
    if (!pre || !*pre) return 0;
    char path[4096]; snprintf(path, sizeof path, "%s.%d", pre, g_recsin_n++);
    FILE *f = fopen(path, "r");
    *out = NULL; *nout = 0;
    if (!f) { fprintf(stderr, "MCCE_RECSIN: %s missing - no records\n", path); return 1; }
    char line[1024]; int cap = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] != 'R') continue;
        McceRec o; memset(&o, 0, sizeof o);
        char id; unsigned ce, mf; double v[11];
        if (sscanf(line, "R %c %la %la %la %la %la %la %la %la %la %la %x %x %la", &id, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
                   &v[7], &v[8], &v[9], &ce, &mf, &v[10]) != 14) continue;
        o.id = id; o.x1 = v[0]; o.y1 = v[1]; o.x2 = v[2]; o.y2 = v[3]; o.cx = v[4]; o.cy = v[5]; o.r = v[6]; o.sa = v[7]; o.sw = v[8];
        o.z1 = v[9]; o.z0 = *nout ? (*out)[*nout - 1].z1 : v[9]; o.ce = (uint8_t)ce; o.mf = mf; o.feed = v[10];
        if (*nout == cap) { cap = cap ? 2 * cap : 256; *out = realloc(*out, sizeof(McceRec) * cap); }
        (*out)[(*nout)++] = o;
    }
    fclose(f);
    return 1;
}
