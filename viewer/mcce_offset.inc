/* mcce_offset.inc - included at the end of mcce_ctour.c (after mcce_sobool.inc): mcce.dll's MCCECalcOffset (the CVe<CSh*>
 * overload, body 0x1800d36c0, decomp/mcce_all.c l.7804-7960; formats/mcce_port_plan.md §9 "M2 offset", formats/mcce_engines.md
 * §6.13.1). The region offset: d < 0 shrinks the region given by the shapes' nesting levels (+0x54), d > 0 grows it (the
 * frame of FUN_1800d92f0 makes the given shapes islands of a frame box).
 *
 * It shares MCCECalcContourOffset's core (mcce_ctour.c: the raw ring FUN_1800dbce0, the loop removal FUN_1800c7bc0, the ring
 * filter FUN_1800d4400) and the region boolean (mcce_sobool.inc). New here: the nest groups FUN_1800d50d0 / FUN_1800d4e80 /
 * FUN_1800d4fd0, the frame ring FUN_1800d48b0, the group assembly FUN_1800d3d30 with FUN_180152cb0 (outer-derived rings minus
 * the island-derived ones) and FUN_18015b1c0, the tail FUN_1800d3b60 / FUN_1800d3cc0 / FUN_1800da6a0 / FUN_1800d9530.
 * Checked against the DLL with tools/tests/off_oracle.sh (peloader --off; both sides print the same stages).
 *
 * Ported: one pass (n = 1), flags without the clean-up bits of FUN_1800da1c0 (bits 1 / 2 / 4 / 8 / 0x10 / 0x20) and with
 * Infinite look ahead (bit 0x20000 clear) - the region-set calls of FUN_180062a70 (flags 0x100, l.56628 / 56710 / 56713).
 * Refused (McceResult.refused): anything else, point shapes ('P', FUN_18011f2e0), more than 64 shapes in one nesting tree. */

static _Thread_local FILE *g_off_dump;   /* the unit oracle's stage dump (cam_cli sooff) */
static void off_pr(const char *stage, const SL *l, int from) {
    if (!g_off_dump) return;
    fprintf(g_off_dump, "stage %s %d\n", stage, l->n - from);
    for (int i = from; i < l->n; i++) {
        const Sh *s = l->s[i];
        fprintf(g_off_dump, "s %d dir %d isl %d n %d box %.17g %.17g %.17g %.17g\n", i - from, s->dir, s->isl, s->n, s->box[0], s->box[1], s->box[2], s->box[3]);
        for (int k = 0; k < s->n; k++) {
            const Ent *e = &s->e[k];
            if (e->id == 'A') fprintf(g_off_dump, " A %.17g %.17g %.17g %.17g %.17g ce %d\n", e->a[0], e->a[1], e->a[2], e->a[3], e->a[4], e->ce);
            else fprintf(g_off_dump, " %c %.17g %.17g %.17g %.17g ce %d\n", e->id, e->a[0], e->a[1], e->a[2], e->a[3], e->ce);
        }
    }
}

/* FUN_1800dbce0 (l.118764-119112) for one shape src of a group, with FUN_1800d47b0 (l.114620-114648): its rings appended to R,
   each with the source list {src} (l.118909-118934: the raw ring's +0x78; FUN_1800c6c90 l.107480-107505 copies it to every
   ring cut from it; FUN_1800db4b0 l.118650-118668 for a circle) */
static const char *off_rings(St *st, Sh *src, SL *R) {
    st->nmit = 0;                                          /* the MITER list +0x70 cleared (l.118938-118942) */
    Sh ring;
    if (src->n == 1 && src->e[0].id == 'A') {
        /* FUN_1800db4b0 (l.118508-118670): the offset circle goes to the output directly, the shape's dir, no filter */
        if (!raw_circle(st, src, &ring)) return NULL;
        Sh *o = malloc(sizeof *o); *o = ring;
        sh_box(o); o->isl = 0; o->frame = 0;
        o->nsrc = 1; o->unm = !src->revmark; o->srcisl = src->isl & 1;
        sl_add(R, o);
        return NULL;
    }
    const char *why = raw_ring(st, src, &ring);
    if (why) { free(ring.e); return why; }
    IsisL isis; memset(&isis, 0, sizeof isis);
    isis_build(&ring, 0, &isis);
    Sh *rings = NULL; int nr = 0;
    if (!isis.n) {
        /* FUN_1800c7bc0 l.107931-107975: no crossing - the ring itself unless FUN_1800c8da0 empties it or FUN_1800c7f00 drops it */
        if (!flagged_longer(&ring) && shape_kept(st, &ring)) { rings = malloc(sizeof(Sh)); rings[0] = ring; nr = 1; ring.e = NULL; }
    } else {
        isis_mark(st, src, &ring, &isis);
        isis_propagate(&isis);
        isis_quarter(st, src, &ring, &isis);
        why = cut_rings_lookahead(st, &ring, &isis, &rings, &nr);
    }
    free(isis.r);
    free(ring.e);
    if (why) { for (int i = 0; i < nr; i++) free(rings[i].e); free(rings); return why; }
    /* FUN_1800c7bc0 l.107995 DetNestLvlShAr, then FUN_1800d4400 (l.114481-114564): the filter (FUN_1800d4660 for a source with +0x54
       bit 0, FUN_1800d4720 otherwise); a rejected even ring of an island source takes the odd rings before it; DetNestLvlShAr */
    Sh **sl = malloc((size_t)(nr ? nr : 1) * sizeof(Sh *));
    int ns = nr;
    for (int i = 0; i < nr; i++) { sh_box(&rings[i]); sl[i] = &rings[i]; }
    if (nr > 64 || det_nest(sl, nr) < 0) { for (int i = 0; i < nr; i++) free(rings[i].e); free(rings); free(sl); return "more than 64 rings in one DetNestLvlShAr (the port's tree limit)"; }
    Sh sb = *src; sb.isl = src->isl & 1;
    sh_box(&sb);
    for (int i = 0; i < ns; i++) {
        if (ring_keep_lookahead(&sb, sl[i])) continue;
        int st_i = i;
        if (sb.isl && !(sl[i]->isl & 1)) { int j = i - 1; while (j >= 0 && (sl[j]->isl & 1)) j--; st_i = j + 1; }
        int cnt = i - st_i + 1;
        for (int k = st_i; k < st_i + cnt; k++) { free(sl[k]->e); sl[k]->e = NULL; sl[k]->n = 0; }
        memmove(sl + st_i, sl + st_i + cnt, (size_t)(ns - st_i - cnt) * sizeof(Sh *));
        ns -= cnt; i = st_i - 1;
    }
    det_nest(sl, ns);
    for (int i = 0; i < ns; i++) {
        Sh *o = malloc(sizeof *o); *o = *sl[i]; sl[i]->e = NULL;
        o->frame = 0; o->nsrc = 1; o->unm = !src->revmark; o->srcisl = src->isl & 1;
        sl_add(R, o);
    }
    for (int i = 0; i < nr; i++) free(rings[i].e);
    free(rings); free(sl);
    return NULL;
}

/* FUN_1800d48b0 (l.114651-114840): the frame shape's ring - the box of the group's rings so far grown by |d| (+0x48), four
   lines FUN_180124c50 (so_rect), every entity +0x12 = 1, +0x90 bit 0 from the frame, source list {frame}; nothing when the
   group has no ring yet */
static void off_frame(const St *st, const Sh *fr, SL *R) {
    if (!R->n) return;
    double mn[2] = { R->s[0]->box[0], R->s[0]->box[1] }, mx[2] = { R->s[0]->box[2], R->s[0]->box[3] };
    for (int i = 1; i < R->n; i++) {
        const double *b = R->s[i]->box;
        if (b[0] <= mn[0]) mn[0] = b[0];
        if (b[1] <= mn[1]) mn[1] = b[1];
        if (mx[0] <= b[2]) mx[0] = b[2];
        if (mx[1] <= b[3]) mx[1] = b[3];
    }
    mn[0] -= st->dist; mn[1] -= st->dist; mx[0] += st->dist; mx[1] += st->dist;
    Sh *f = so_rect(mn, mx);
    for (int k = 0; k < f->n; k++) f->e[k].ce = 1;
    f->frame = fr->frame & 1;
    f->nsrc = 1; f->unm = !fr->revmark; f->srcisl = fr->isl & 1;
    sl_add(R, f);
}

/* FUN_180152cb0 (l.182106-182300): A (copied) minus each shape b of B in turn: a shape of the copy that b cuts (FUN_18015dc60 =
   the core op 1 + FUN_1801239f0's small-shape removal, so_core_tol) is replaced in place by the pieces; otherwise FUN_18013e910:
   a shape inside b (or on it all along, param_5) is removed, b inside a shape is copied to the hole list (CSh::operator=) and
   the next b is taken. Out: the copy, then the holes */
static void off_minus(double t, const SL *A, const SL *B, SL *out) {
    SL X = { 0 }, H = { 0 };
    sl_copy_into(A, &X);
    for (int j = 0; j < B->n; j++) {
        for (int i = 0; i < X.n; i++) {
            SL R = { 0 };
            if (so_core_tol(1, t, X.s[i], B->s[j], &R)) {
                sl_del(&X, i);
                for (int k = 0; k < R.n; k++) sl_ins(&X, i + k, R.s[k]);
                i += R.n - 1;
                free(R.s);
                continue;
            }
            sl_fre(&R);
            int pa = 0, pb = 0, pe = 0;
            shape_in_shape3(X.s[i], B->s[j], &pa, &pb, &pe);
            if (pa == 1 || pe) { sl_del(&X, i); i--; }
            else if (pb == 1) { sl_add(&H, so_dup(B->s[j])); break; }
        }
    }
    sl_move(&X, out); sl_move(&H, out);
    free(X.s); free(H.s);
}

/* FUN_1800d3d30 (l.114216-114389): the rings of one nest group into the output */
static void off_group(double t, SL *R, SL *out) {
    SL I = { 0 }, O = { 0 }, L60 = { 0 }, L88 = { 0 }, Ld8 = { 0 };
    /* FUN_1800d4170 (l.114394-114429): rings with a direction and a source list, by their first source's +0x54 bit 0 */
    for (int i = 0; i < R->n; i++) {
        Sh *r = R->s[i];
        if (r->dir != 0 && r->nsrc > 0) sl_add(r->srcisl ? &I : &O, r);
        else so_free(r);
    }
    R->n = 0;
    if (!I.n) { sl_move(&O, out); free(O.s); free(I.s); return; }   /* l.114280-114298: the outer-derived rings as they are */
    if (I.n == 1) sl_move(&I, &L60);
    else {
        so_union_ar(t, &I, &L60);                       /* l.114289 SoPockUnionAr(+0x50) */
        /* FUN_1800d42a0 (l.114432-114478): shapes with +0x54 != 0 moved to L88 */
        for (int i = 0; i < L60.n; i++) if (L60.s[i]->isl != 0) { sl_add(&L88, L60.s[i]); memmove(L60.s + i, L60.s + i + 1, (size_t)(L60.n - i - 1) * sizeof(Sh *)); L60.n--; i--; }
        if (L88.n) {
            /* FUN_18015b1c0 (l.186989-187020): FUN_1801638f0 (so_isect2) for every outer-derived ring x every L88 shape */
            for (int a = 0; a < O.n; a++) for (int b = 0; b < L88.n; b++) so_isect2(t, O.s[a], L88.s[b], &Ld8);
            sl_clr(&L88);
            sl_move(&Ld8, &L88);
        }
    }
    sl_fre(&I);
    /* l.114304-114314: the frame alone - L60 and it; otherwise FUN_180152cb0; then L88, DetNestLvlShAr */
    if (O.n == 1 && (O.s[0]->frame & 1)) { sl_move(&L60, &Ld8); sl_move(&O, &Ld8); }
    else off_minus(t, &O, &L60, &Ld8);
    sl_move(&L88, &Ld8);
    so_nest(&Ld8);
    sl_move(&Ld8, out);
    sl_fre(&O); sl_fre(&L60); sl_fre(&L88); free(Ld8.s);
}

/* FUN_1800da6a0 (l.118079-118125) with FUN_1800d9530 (l.117550-117589): the start entity = the one whose source has the lowest
   index over the whole input list (base[i] = the entity count of the input shapes before shape i); one earlier when that is an
   arc preceded by a line with CeFlags bit 2; FUN_18014fd50 rotates */
static void off_rotate(Sh *s, const int *base, int nbase) {
    int best = -1, lo = 0x7fffffff;
    for (int i = 0; i < s->n; i++) {
        int sh = s->e[i].src_sh, k = s->e[i].src_ent;
        if (sh < 0 || sh >= nbase || k < 0) continue;
        int idx = base[sh] + k;
        if (idx < lo) { lo = idx; best = i; }
    }
    if (best < 0) return;
    if (s->e[best].id == 'A') {
        int p = best == 0 ? s->n - 1 : best - 1;
        if (s->e[p].id == 'L' && (s->e[p].ce & 2)) best = p;
    }
    if (best <= 0 || s->dir == 0) return;
    Ent *t = malloc((size_t)s->n * sizeof(Ent));
    memcpy(t, s->e + best, (size_t)(s->n - best) * sizeof(Ent));
    memcpy(t + (s->n - best), s->e, (size_t)best * sizeof(Ent));
    memcpy(s->e, t, (size_t)s->n * sizeof(Ent));
    free(t);
}

McceResult mcce_offset(const McceShape *in, int nin, int npass, double d, unsigned flags, double ang, double t1, double t2, void *dump) {
    McceResult res; memset(&res, 0, sizeof res);
    unsigned fl = flags & 0xfff8ffff;                    /* l.7915 */
    if (npass != 1) { res.refused = "MCCECalcOffset with more than one pass (n != 1) not ported"; return res; }
    if (fl & 0x20000) { res.refused = "MCCECalcOffset with Infinite look ahead off (0x20000) not ported"; return res; }
    g_off_dump = dump; g_so_refused = NULL;
    SL T = { 0 }, OUT = { 0 };
    int *base = calloc((size_t)(nin ? nin : 1), sizeof(int));
    for (int i = 0, b = 0; i < nin; i++) { base[i] = b; b += in[i].n; }
    if (g_off_dump) {   /* stage "in": the input as given (before FUN_1800da1c0), as the DLL side prints it */
        SL R0 = { 0 };
        for (int i = 0; i < nin; i++) {
            Sh *s = calloc(1, sizeof *s);
            for (int k = 0; k < in[i].n; k++) { Ent e = in[i].e[k]; e.ce = 0; ent_box(&e); sh_push(s, &e); }
            s->dir = in[i].dir; s->isl = in[i].isl; sh_box(s); sl_add(&R0, s);
        }
        off_pr("in", &R0, 0); sl_fre(&R0);
    }
    /* AppendByCopy + FUN_1800da810 (l.118128-118170: each entity's +0x70 = its input entity) + FUN_1800da1c0 with these flags
       (l.117939-118048: CeFlags 0; bit 0x20000 clear: FUN_1800d99e0, a shape of identical entities reduced to one) */
    for (int i = 0; i < nin; i++) {
        Sh *s = calloc(1, sizeof *s);
        for (int k = 0; k < in[i].n; k++) {
            Ent e = in[i].e[k]; e.ce = 0; e.src_sh = i; e.src_ent = k; ent_box(&e); sh_push(s, &e);
            if (e.id != 'L' && e.id != 'A') { res.refused = "MCCECalcOffset: a point shape ('P', FUN_18011f2e0) not ported"; }
        }
        s->dir = in[i].dir; s->isl = in[i].isl;
        if (s->n > 1) {   /* FUN_1800d99e0 (l.117732-117760) / FUN_1800d9b40 (l.117765-117891) */
            int all = 1;
            for (int k = 1; k < s->n && all; k++) {
                const Ent *a = &s->e[k], *b = &s->e[0];
                P2 pa = ent_start(a), pb = ent_start(b), qa = ent_end(a), qb = ent_end(b);
                if (a->id != b->id || !(fabs(pa.x - pb.x) < 1e-06) || !(fabs(pa.y - pb.y) < 1e-06) || !(fabs(qa.x - qb.x) < 1e-06) || !(fabs(qa.y - qb.y) < 1e-06)) all = 0;
                else if (a->id == 'A' && (!(fabs(a->a[0] - b->a[0]) < 1e-06) || !(fabs(a->a[1] - b->a[1]) < 1e-06) || a->a[4] * b->a[4] < 0.0)) all = 0;
            }
            if (all) s->n = 1;
        }
        if (fl & 1) {
            /* bit 1 (l.117954-117986): the closedness test is skipped (param_2 = (+0x38 == 0), true here, l.7930-7931); FUN_18012c2b0
               (l.161604-161721, tol 0.0001: an entity of length <= tol whose neighbours meet within tol is removed, n > 2);
               FUN_18012c740 (gap closing, link threshold 1e-10, 1e-7 with 0x20000) - the code of mcce_contour_offset */
            for (int k = 0; k < s->n; k++) {
                if (e_len(&s->e[k]) <= 0.0001 && s->n > 2) {
                    int ip = k == 0 ? s->n - 1 : k - 1, in_ = k < s->n - 1 ? k + 1 : 0;
                    double x0, y0, x1, y1;
                    e_end(&s->e[ip], &x0, &y0); e_start(&s->e[in_], &x1, &y1);
                    if ((y1 - y0) * (y1 - y0) + (x1 - x0) * (x1 - x0) <= 0.0001 * 0.0001) {
                        memmove(s->e + k, s->e + k + 1, (size_t)(s->n - k - 1) * sizeof(Ent));
                        s->n--; k--;
                    }
                }
            }
            close_gaps(s, (fl & 0x20000) ? 1e-07 : 1e-10);
        }
        sh_box(s);
        if (fl & 0x20) {
            /* bit 0x20 (l.118021-118026): FUN_180132ef0 (param_4 = +0x50 = t1) replaces the shape by the pieces of ReconfigSelfIsectSh
               (mcce_contour_offset's self_split, each piece with the parent's +0x54); bit 8 below then sees the last piece (iVar12) */
            Sh *pl = NULL; int np = 0;
            const char *w = self_split(s, t1, &pl, &np);
            if (w) { res.refused = w; free(s->e); free(s); break; }
            if (np) {
                for (int k = 0; k < np; k++) {
                    for (int q = 0; q < pl[k].n; q++) ent_box(&pl[k].e[q]);
                    if (pl[k].dir == 0) res.refused = "MCCECalcOffset: an open piece from the self-crossing split (FUN_180132ef0) not ported";
                    Sh *p = malloc(sizeof *p); *p = pl[k]; p->isl = s->isl; sh_box(p);
                    if (k < np - 1) sl_add(&T, p); else { free(s->e); free(s); s = p; }
                }
                free(pl);
            }
        }
        if (fl & 8) { double ar; s->dir = sh_orient_t(s, 0.001, &ar); }   /* l.118028-118036: FUN_1801270c0(0.001) (param_2 true) */
        sl_add(&T, s);
    }
    if (!res.refused && ((fl & 0x10) || ((fl & 0x20) && T.n != nin))) so_nest(&T);   /* l.118038-118046: FUN_18012d040 (closed before open) + DetNestLvlShAr */
    if (res.refused) goto done;

    if (!(1e-06 < fabs(d) || fabs(d) == 1e-06)) {   /* l.7958-7960: the copy */
        for (int i = 0; i < T.n; i++) sl_add(&OUT, so_dup(T.s[i]));
        goto out;
    }
    St st; memset(&st, 0, sizeof st);
    st.flags = fl; st.dist = fabs(d); st.t1 = t1; st.t2 = t2; st.ang_cos = cos(ang * 0.017453292519943295);
    /* FUN_1800da180 (l.117926-117936): d > 0 the frame FUN_1800d92f0 (l.117465-117547: GetMinMaxViaSh of the shapes' boxes
       grown by 2.5 |d| n, FUN_180124c50, +0x90 bit 0, DetNestLvlShAr); FUN_1800da5e0 (l.118051-118075): outers (+0x54 bit 0
       clear) to dir -1, islands to +1, RevSh with the +0x90 bit 1 mark */
    if (0.0 < d && T.n) {
        double mn[2] = { T.s[0]->box[0], T.s[0]->box[1] }, mx[2] = { T.s[0]->box[2], T.s[0]->box[3] };
        for (int i = 0; i < T.n; i++) {
            const double *b = T.s[i]->box;
            if (b[0] <= mn[0]) mn[0] = b[0];
            if (b[1] <= mn[1]) mn[1] = b[1];
            if (mx[0] <= b[2]) mx[0] = b[2];
            if (mx[1] <= b[3]) mx[1] = b[3];
        }
        double g = fabs(d) * (double)npass * 2.5;
        mn[0] -= g; mn[1] -= g; mx[0] += g; mx[1] += g;
        Sh *f = so_rect(mn, mx); f->frame = 1;
        sl_add(&T, f);
        so_nest(&T);
    }
    for (int i = 0; i < T.n; i++) {
        Sh *s = T.s[i];
        if ((!(s->isl & 1) && s->dir == 1) || ((s->isl & 1) && s->dir == -1)) { sh_rev(s); s->revmark = 1; }
    }
    /* FUN_1800d50d0 (l.114916-114998): per nest group (FUN_1800d4e80 l.114847-114880: shapes up to and with the first one whose
       +0x54 bit 0 is clear), each shape: the frame -> FUN_1800d48b0, else FUN_1800dbce0 when FUN_1800d4fd0 (l.114883-114913:
       the box diagonal - of the group's last shape for an island, unless that one is an island too: then always - is at least
       1.97 |d|); then FUN_1800d3d30 */
    for (int g0 = 0; g0 < T.n && !res.refused; ) {
        int g1 = g0;
        while (g1 < T.n && (T.s[g1]->isl & 1)) g1++;
        if (g1 >= T.n) g1 = T.n - 1;
        SL R = { 0 };
        for (int i = g0; i <= g1 && !res.refused; i++) {
            Sh *s = T.s[i];
            if (s->frame & 1) { off_frame(&st, s, &R); continue; }
            const Sh *b = s;
            if (s->isl & 1) { if (T.s[g1]->isl & 1) b = NULL; else b = T.s[g1]; }
            if (b) {
                double dx = b->box[0] - b->box[2], dy = b->box[1] - b->box[3];
                if (!(st.dist * 1.97 <= sqrt(dy * dy + dx * dx))) continue;
            }
            const char *w = off_rings(&st, s, &R);
            if (w) res.refused = w;
        }
        if (res.refused) { sl_fre(&R); break; }
        off_pr("rings", &R, 0);
        int n0 = OUT.n;
        off_group(st.t1, &R, &OUT);
        free(R.s);
        off_pr("group", &OUT, n0);
        if (g_so_refused) { res.refused = g_so_refused; break; }
        g0 = g1 + 1;
    }
    free(st.mit);
    if (res.refused) goto done;
    /* (one pass: no parity toggle of l.7943-7953) FUN_1800d3b60 (l.114136-114182): d > 0: frame-derived shapes (+0x90 bit 0) erased,
       the others' +0x54 + 1 (n odd); FUN_1800d3cc0 (l.114185-114208): reversed back when every source carries the mark;
       FUN_1800da6a0 the start; FUN_1800d8d90 (l.117293-117338, bit 0x100 set: FUN_1800d6180 only); DetNestLvlShAr */
    off_pr("pre", &OUT, 0);
    for (int i = 0; i < OUT.n; i++) {
        Sh *s = OUT.s[i];
        if (0.0 < d) {
            if (s->frame & 1) { sl_del(&OUT, i); i--; continue; }
            if (npass & 1) s->isl += 1;
        }
        if (s->nsrc > 0 && s->unm == 0) sh_rev(s);
        off_rotate(s, base, nin);
    }
    if (OUT.n) {
        Sh *a = malloc((size_t)(OUT.n > 0 ? OUT.n : 1) * sizeof(Sh));
        for (int i = 0; i < OUT.n; i++) { sh_box(OUT.s[i]); a[i] = *OUT.s[i]; }
        tiny_cull(a, OUT.n);                             /* FUN_1800d6180 (l.115607-115638) */
        for (int i = 0, j = 0; i < OUT.n; i++, j++) if (!a[j].n) { sl_del(&OUT, i); i--; }
        free(a);
    }
    if (!(fl & 0x100))   /* FUN_1800d8d90 l.117308-117336, bit 0x100 clear: FUN_1800d62d0 + FUN_1800d7100 (tail_shape) on each shape, an emptied one erased */
        for (int i = 0; i < OUT.n; i++) { tail_shape(&st, OUT.s[i]); if (!OUT.s[i]->n) { sl_del(&OUT, i); i--; } }
    so_nest(&OUT);
    if (g_so_refused) { res.refused = g_so_refused; goto done; }
out:
    off_pr("result", &OUT, 0);
    for (int i = 0; i < OUT.n; i++) { Sh *s = OUT.s[i]; out_push(&res, s->e, s->n, s->dir, s->isl); s->e = NULL; }
done:
    sl_fre(&T); sl_fre(&OUT); free(base);
    g_off_dump = NULL;
    if (res.refused) { McceResult r2; memset(&r2, 0, sizeof r2); r2.refused = res.refused; mcce_shapes_free(&res); return r2; }
    return res;
}

/* mcce::UpdDirShAr(tol) (l.5106-5125: each shape's +0x50 = FUN_1801270c0(tol), the direction of FUN_180126ac0's area) then
   mcce::DetNestLvlShAr (l.7733-7792): s reordered (map along with it), isl = the nesting level. Returns -1 past 64 shapes. */
static int dir_nest_impl(McceShape *s, int n, int *map, int orient);
int mcce_dir_nest(McceShape *s, int n, int *map) { return dir_nest_impl(s, n, map, 1); }
/* DetNestLvlShAr alone (the callers that do not run UpdDirShAr first, e.g. FUN_180090ae0 l.80150): each shape keeps its dir */
int mcce_nest_only(McceShape *s, int n, int *map) { return dir_nest_impl(s, n, map, 0); }
static int dir_nest_impl(McceShape *s, int n, int *map, int orient) {
    if (n <= 0) return 0;
    if (n > 64) return -1;
    Sh *T = calloc((size_t)n, sizeof(Sh)); Sh **P = malloc((size_t)n * sizeof(Sh *));
    for (int i = 0; i < n; i++) {
        T[i].e = s[i].e; T[i].n = T[i].cap = s[i].n;
        for (int k = 0; k < s[i].n; k++) ent_box(&T[i].e[k]);
        sh_box(&T[i]);
        double ar; T[i].dir = orient ? sh_orient_t(&T[i], 0.001, &ar) : s[i].dir;
        T[i].isl = s[i].isl; T[i].src_rev = i;          /* src_rev: the index before the reordering (bookkeeping here only) */
        P[i] = &T[i];
    }
    det_nest(P, n);
    McceShape *o = malloc((size_t)n * sizeof *o); int *mo = map ? malloc((size_t)n * sizeof(int)) : NULL;
    for (int i = 0; i < n; i++) {
        int k = P[i]->src_rev;
        o[i] = s[k]; o[i].dir = P[i]->dir; o[i].isl = P[i]->isl;
        if (mo) mo[i] = map[k];
    }
    memcpy(s, o, (size_t)n * sizeof *o);
    if (mo) { memcpy(map, mo, (size_t)n * sizeof(int)); free(mo); }
    free(o); free(T); free(P);
    return 0;
}

/* the unit oracle's port side (tools/peloader/mcce_off.c "OFF 1": n, d, flags (hex), ang, tol T1 T2, X n shapes "s DIR LEVEL N") */
int mcce_offset_file(const char *in, const char *outp) {
    FILE *f = fopen(in, "r");
    if (!f) return 2;
    char line[512]; int n = 1, nx = 0, rc = 0; double d = 0, ang = 0, t1 = 1e-6, t2 = 1e-6; unsigned fl = 0x100; McceShape *X = NULL;
    if (!fgets(line, sizeof line, f) || strncmp(line, "OFF 1", 5)) { fclose(f); return 2; }
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "end", 3)) break;
        if (!strncmp(line, "n ", 2)) n = atoi(line + 2);
        else if (!strncmp(line, "d ", 2)) d = strtod(line + 2, NULL);
        else if (!strncmp(line, "flags ", 6)) fl = (unsigned)strtoul(line + 6, NULL, 16);
        else if (!strncmp(line, "ang ", 4)) ang = strtod(line + 4, NULL);
        else if (!strncmp(line, "tol ", 4)) { char *p = line + 4; t1 = strtod(p, &p); t2 = strtod(p, NULL); }
        else if (!strncmp(line, "X ", 2)) {
            nx = atoi(line + 2);
            X = calloc((size_t)(nx ? nx : 1), sizeof(McceShape));
            for (int i = 0; i < nx && !rc; i++) {
                int dir, isl, m;
                if (!fgets(line, sizeof line, f) || sscanf(line, "s %d %d %d", &dir, &isl, &m) != 3) { rc = 2; break; }
                McceShape *s = &X[i];
                s->e = calloc((size_t)(m ? m : 1), sizeof(McceEnt)); s->n = m; s->dir = dir; s->isl = isl;
                for (int k = 0; k < m; k++) {
                    if (!fgets(line, sizeof line, f)) { rc = 2; break; }
                    char *p = line; while (*p == ' ') p++;
                    McceEnt *e = &s->e[k]; e->id = *p++;
                    for (int c = 0; c < (e->id == 'A' ? 5 : 4); c++) e->a[c] = strtod(p, &p);
                }
            }
        }
    }
    fclose(f);
    FILE *o = rc ? NULL : fopen(outp, "w");
    if (o) {
        McceResult r = mcce_offset(X, nx, n, d, fl, ang, t1, t2, o);
        if (r.refused) fprintf(o, "# refused: %s\n", r.refused);
        mcce_shapes_free(&r);
        fclose(o);
    } else rc = 2;
    for (int i = 0; i < nx; i++) free(X[i].e);
    free(X);
    return rc;
}
