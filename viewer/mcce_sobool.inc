/* mcce_sobool.inc - included at the end of mcce_ctour.c (it uses that file's DLL-exact kernels: isab_list = FUN_180133c70,
 * sec_in_shape = FUN_1801489a0, shape_in_shape = FUN_18013e910, det_nest = DetNestLvlShAr, close_gaps = FUN_18012c740,
 * close_forward = FUN_18014b720, sh_orient_t = FUN_180126ac0, sh_rev = RevSh).
 *
 * Region boolean of mcce.dll (formats/mcce_port_plan.md §9 "M3"): FUN_180153b30 (X minus Y), FUN_18015b2e0 (the
 * pairwise intersection of two region groups), SoPockUnionAr (the union) and their shape-level core FUN_180162010.
 * Region lists are in mcce's group order [islands..., outer] (CSh +0x54 bit 0 = island; after DetNestLvlShAr +0x54 =
 * the nesting level). The citations are decomp/mcce_all.c line numbers; checked step by step against the DLL with
 * tools/tests/sobool_oracle.sh (peloader --sobool; both sides print the same stages).
 * Not ported: FUN_18015f270 (the bUseOldBuildSoFx = 1 trace; registry Toolpaths\Util, default 0: FUN_180162a00
 * l.191202-191226 - the default trace FUN_18015eea0 is the one used, as the DLL does without that value). Refused (so_refuse):
 * more than 64 shapes in one DetNestLvlShAr (the tree size of mcce_ctour.c's det_nest).
 * The CSh +0x68 child lists FUN_18015fda0 copies (l.189770-189800) are empty for every input built here (not kept). */
static _Thread_local FILE *g_so_dump;   /* the unit oracle's stage dump (cam_cli sobool) */
static void so_refuse(const char *why) { if (!g_so_refused) g_so_refused = why; }
static Sh *so_dup(const Sh *s) { Sh *d = malloc(sizeof *d); sh_copy(d, s); return d; }
static void so_free(Sh *s) { if (s) { free(s->e); free(s); } }
static void sl_add(SL *l, Sh *s) { if (l->n == l->cap) { l->cap = l->cap ? 2 * l->cap : 8; l->s = realloc(l->s, (size_t)l->cap * sizeof(Sh *)); } l->s[l->n++] = s; }
static void sl_ins(SL *l, int at, Sh *s) { sl_add(l, NULL); memmove(l->s + at + 1, l->s + at, (size_t)(l->n - 1 - at) * sizeof(Sh *)); l->s[at] = s; }
static void sl_del(SL *l, int at) { so_free(l->s[at]); memmove(l->s + at, l->s + at + 1, (size_t)(l->n - at - 1) * sizeof(Sh *)); l->n--; }
static void sl_pop0(SL *l) { memmove(l->s, l->s + 1, (size_t)(l->n - 1) * sizeof(Sh *)); l->n--; }
static void sl_clr(SL *l) { for (int i = 0; i < l->n; i++) so_free(l->s[i]); l->n = 0; }
static void sl_fre(SL *l) { sl_clr(l); free(l->s); l->s = NULL; l->cap = 0; }
static void sl_move(SL *src, SL *dst) { for (int i = 0; i < src->n; i++) sl_add(dst, src->s[i]); src->n = 0; }   /* mcce::AppendByMove (l.5394-5451) */
static void sl_copy_into(const SL *src, SL *dst) { for (int i = 0; i < src->n; i++) sl_add(dst, so_dup(src->s[i])); }   /* mcce::AppendByCopy (l.13582-13670) */
static int so_isl(const Sh *s) { return s->isl & 1; }
static void so_nest(SL *l) { if (det_nest(l->s, l->n) < 0) so_refuse("more than 64 shapes in one DetNestLvlShAr (the port's tree limit)"); }
static void so_pr(const char *stage, const SL *l) {
    if (!g_so_dump) return;
    fprintf(g_so_dump, "stage %s %d\n", stage, l->n);
    for (int i = 0; i < l->n; i++) {
        const Sh *s = l->s[i];
        fprintf(g_so_dump, "s %d dir %d isl %d n %d box %.17g %.17g %.17g %.17g\n", i, s->dir, s->isl, s->n, s->box[0], s->box[1], s->box[2], s->box[3]);
        for (int k = 0; k < s->n; k++) {
            const Ent *e = &s->e[k];
            if (e->id == 'A') fprintf(g_so_dump, " A %.17g %.17g %.17g %.17g %.17g\n", e->a[0], e->a[1], e->a[2], e->a[3], e->a[4]);
            else fprintf(g_so_dump, " %c %.17g %.17g %.17g %.17g\n", e->id, e->a[0], e->a[1], e->a[2], e->a[3]);
        }
    }
}

/* FUN_180146e20 (l.175722-176061) with param_7 = full: the section (e1, t1) -> (e2, t2) appended to out; full (start ==
   end within 1e-12) = the whole loop; a degenerate first / last piece (< 1e-12) dropped (trimEnds FUN_1801483e0
   l.176504-176617, the count FUN_180147ed0 l.176360-176395) - the code of viewer/mcce_dyn.c sh_section_full */
static int sec_count_f(const Sh *s, int e1, double t1, int e2, double t2, int *full) {
    if (*full && (e1 != e2 || fabs(t1 - t2) > 1e-12)) *full = 0;
    if (e2 < e1 || (e1 == e2 && t2 < t1) || *full) return *full ? s->n + 1 : e2 + 1 + (s->n - e1);
    return e2 - e1 + 1;
}
static void sec_append_full(const Sh *s, int e1, double t1, int e2, double t2, Sh *out, int full) {
    if (!s->n) return;
    t1 = e_clamp(t1); t2 = e_clamp(t2);
    if (full && (e1 != e2 || fabs(t1 - t2) >= 1e-12)) full = 0;
    int n = sec_count_f(s, e1, t1, e2, t2, &full);
    if (n != 1) {
        const Ent *a = &s->e[e1];
        double rest = a->id == 'L' ? hypot(a->a[2] - ent_at(a, t1).x, a->a[3] - ent_at(a, t1).y) : a->id == 'A' ? fabs((a->a[4] - a->a[4] * t1) * a->a[2]) : 0;
        if ((a->id != 'L' && a->id != 'A') || rest < 1e-12) { full = 0; n--; t1 = 0.0; next_ent(s, &e1); }
        const Ent *b = &s->e[e2];
        double head = b->id == 'L' ? hypot(ent_at(b, t2).x - b->a[0], ent_at(b, t2).y - b->a[1]) : b->id == 'A' ? fabs(b->a[4] * t2 * b->a[2]) : 0;
        if ((b->id != 'L' && b->id != 'A') || head < 1e-12) { full = 0; n--; t2 = 1.0; if (e2 > 0) e2--; else if (s->dir != 0) e2 = s->n - 1; }
    } else {
        const Ent *a = &s->e[e1];
        double piece = a->id == 'L' ? hypot(ent_at(a, t2).x - ent_at(a, t1).x, ent_at(a, t2).y - ent_at(a, t1).y) : a->id == 'A' ? fabs((t2 - t1) * a->a[4] * a->a[2]) : 0;
        if ((a->id != 'L' && a->id != 'A') || piece < 1e-12) { n = 0; full = 0; }
    }
    if (n <= 0) return;
    n = sec_count_f(s, e1, t1, e2, t2, &full);
    int wrap = e2 < e1 || (e1 == e2 && t2 < t1) || full;
    int old = out->n;
    if (wrap) { for (int k = e1; k < s->n; k++) sh_push(out, &s->e[k]); for (int k = 0; k <= e2; k++) sh_push(out, &s->e[k]); }
    else for (int k = e1; k <= e2; k++) sh_push(out, &s->e[k]);
    int got = out->n - old;
    if (got == 1) {
        Ent *r = &out->e[old]; const Ent *b = &s->e[e1];
        if (r->id == 'L') { P2 p0 = ent_at(b, t1), p1 = ent_at(b, t2); r->a[0] = p0.x; r->a[1] = p0.y; r->a[2] = p1.x; r->a[3] = p1.y; }
        else if (r->id == 'A') { double sa = b->a[3], sw = b->a[4]; r->a[3] = sa + sw * t1; r->a[4] = sw * (t2 - t1); }
        ent_box(r);
    } else if (got > 1) {
        Ent *f = &out->e[old], *l = &out->e[out->n - 1]; const Ent *b0 = &s->e[e1], *b1 = &s->e[e2];
        if (f->id == 'L') { P2 q = ent_at(b0, t1); f->a[0] = q.x; f->a[1] = q.y; }
        else if (f->id == 'A') { f->a[3] = f->a[3] + f->a[4] * t1; f->a[4] = f->a[4] - f->a[4] * t1; }
        if (l->id == 'L') { P2 q = ent_at(b1, t2); l->a[2] = q.x; l->a[3] = q.y; }
        else if (l->id == 'A') l->a[4] = l->a[4] * t2;
        ent_box(f); ent_box(l);
    }
    sh_box(out);
}

/* FUN_18013ccc0 (l.170229-170365): the nearest point of shape s to p, entities from hint on (cyclic); an entity is tried
   when its box is not farther than the best so far; stops at a distance below tol2. *ent / *d2 = the entity, squared distance */
static void near_from(double tol2, P2 p, const Sh *s, int hint, int *ent, double *d2) {
    int n = s->n, best = -1; double bd = 0.0;
    if (hint < 0) hint = 0;
    for (int i = hint; i - hint < n; i++) {
        int k = i % n; const double *b = s->e[k].box; double d, x0;
        if (p.x < b[0] || b[2] <= p.x) {
            x0 = p.x < b[0] ? b[0] : b[2];
            if (p.y < b[1] || b[3] <= p.y) { double y0 = p.y < b[1] ? b[1] : b[3]; d = (p.y - y0) * (p.y - y0) + (p.x - x0) * (p.x - x0); }
            else d = (p.x - x0) * (p.x - x0);
        } else {
            if (p.y < b[1]) d = (p.y - b[1]) * (p.y - b[1]);
            else if (p.y < b[3]) d = 0.0;
            else d = (p.y - b[3]) * (p.y - b[3]);
        }
        if (best == -1 || d <= bd) {
            P2 q = ent_nearest(&s->e[k], p);
            d = (p.y - q.y) * (p.y - q.y) + (p.x - q.x) * (p.x - q.x);
            if (best == -1 || d <= bd) { best = k; bd = d; if (d < tol2) break; }
        }
    }
    *ent = best; *d2 = bd;
}
/* the entity's end point as FUN_1801615d0 reads it ('L' +0x28, 'A' the end angle, else +0x18) */
static P2 so_end(const Ent *e) {
    if (e->id == 'L') return p2(e->a[2], e->a[3]);
    if (e->id == 'A') { double g = e->a[4] + e->a[3]; return p2(cos(g) * e->a[2] + e->a[0], sin(g) * e->a[2] + e->a[1]); }
    return p2(e->a[0], e->a[1]);
}
/* the entity's start point as FUN_18015e720 reads it ('L' +0x18, 'A' the start angle, else +0x18) */
static P2 so_start(const Ent *e) {
    if (e->id == 'A') return p2(cos(e->a[3]) * e->a[2] + e->a[0], sin(e->a[3]) * e->a[2] + e->a[1]);
    return p2(e->a[0], e->a[1]);
}
/* FUN_1801615d0 (l.190434-190615): the same shape - as many entities, a's first end point within tol of some end point
   of b (the nearest), from there entity by entity the same kind with end points within tol and no arc over 340 degrees */
static int so_same(double tol, const Sh *a, const Sh *b) {
    int n = a->n;
    if (n != b->n || n == 0) return 0;
    P2 p = so_end(&a->e[0]);
    double best = 3.4028234663852886e+38; int bi = -1;
    for (int j = 0; j < n; j++) {
        P2 q = so_end(&b->e[j]);
        double d = (p.y - q.y) * (p.y - q.y) + (p.x - q.x) * (p.x - q.x);
        if (d < best) { bi = j; best = d; }
    }
    if (tol * tol < best) return 0;
    for (int i = 0; i < n; i++, bi++) {
        const Ent *ea = &a->e[i], *eb = &b->e[bi % n];
        if (ea->id != eb->id) return 0;
        P2 u = so_end(ea), v = so_end(eb);
        if (tol * tol < (u.y - v.y) * (u.y - v.y) + (u.x - v.x) * (u.x - v.x)) return 0;
        if (ea->id == 'A' && (5.934119456780721 < fabs(ea->a[4]) || 5.934119456780721 < fabs(eb->a[4]))) return 0;
    }
    return 1;
}
/* FUN_180161090 (l.190266-190431): two closed shapes lie on each other - every entity's middle and end of a within tol
   of b (FUN_18013ccc0 from the entity found last), then the same for b on a */
static int so_on_each(double tol, const Sh *a, const Sh *b) {
    if (!((a->dir == 1 || a->dir == -1) && (b->dir == 1 || b->dir == -1))) return 0;
    double t2 = tol * tol;
    for (int pass = 0; pass < 2; pass++) {
        const Sh *s = pass ? b : a, *o = pass ? a : b;
        int hint = 0;
        for (int k = 0; k < 2 * s->n; k++) {
            P2 q = ent_at(&s->e[k >> 1], (k & 1) ? 1.0 : 0.5);
            int ent; double d;
            near_from(t2, q, o, hint, &ent, &d);
            if (t2 < d) return 0;
            hint = ent;
        }
    }
    return 1;
}
/* FUN_180161f90 (l.190748-190781): op 0 both counter-clockwise, op 1 a clockwise and b counter-clockwise, op 2 both clockwise */
static void so_orient(int op, Sh *a, Sh *b) {
    int ra, rb;
    if (op == 0) { ra = a->dir != 1; rb = b->dir != 1; }
    else if (op == 1) { ra = a->dir != -1; rb = b->dir != 1; }
    else if (op == 2) { ra = a->dir != -1; rb = b->dir != -1; }
    else return;
    if (ra) sh_rev(a);
    if (rb) sh_rev(b);
}
/* the intersection record with the classes ISAB +0x38 (section of A from it) / +0x3c (section of B from it):
   0 undecided (on the other boundary), 1 kept, 3 dropped, 2 used by a ring (FUN_18015fda0) */
typedef struct { Isab i; int ca, cb; } SoRec;

/* FUN_180128bb0 (l.159716-160013): the shape is a retrace - an even number of entities that pair off from both ends inwards
   (a stack of index ranges [lo, hi]; the entity at lo is matched, alternately from hi downwards and from lo + 1 upwards,
   by an entity of its kind running the other way within tol: a line with swapped ends, an arc with the same centre, radius
   and opposite sweep whose ends meet the other's, a point; the ranges inside and after the pair are pushed) */
static int so_retrace(const Sh *s, double tol) {
    int n = s->n;
    if (n == 0 || (n & 1)) return 0;
    int *L = malloc((size_t)(n + 2) * sizeof(int)), *R = malloc((size_t)(n + 2) * sizeof(int)), top = 0, ok = 1;
    L[0] = 0; R[0] = n - 1; top = 1;
    while (top > 0) {
        int base = L[top - 1], lo = base + 1, hi = R[top - 1], alt = 0, cand = -1, found = 0;
        while (!found) {
            if (hi < lo) break;
            if (alt & 1) cand = lo++; else cand = hi--;
            alt++;
            const Ent *a = &s->e[base], *b = &s->e[cand];
            if (a->id != b->id) continue;
            if (a->id == 'L') {
                if (fabs(a->a[0] - b->a[2]) < tol && fabs(a->a[1] - b->a[3]) < tol && fabs(a->a[2] - b->a[0]) < tol && fabs(a->a[3] - b->a[1]) < tol) found = 1;
            } else if (a->id == 'A') {
                if (fabs(a->a[0] - b->a[0]) < tol && fabs(a->a[1] - b->a[1]) < tol && fabs(a->a[2] - b->a[2]) <= tol && fabs(a->a[4] + b->a[4]) <= tol) {
                    double ge = b->a[4] + b->a[3], gs = a->a[4] * 0.0 + a->a[3];
                    double dx = (cos(gs) * a->a[2] + a->a[0]) - (cos(ge) * b->a[2] + b->a[0]), dy = (sin(gs) * a->a[2] + a->a[1]) - (sin(ge) * b->a[2] + b->a[1]);
                    if (fabs(dx) < tol && fabs(dy) < tol) {
                        ge = b->a[4] * 0.0 + b->a[3]; gs = a->a[4] + a->a[3];
                        dx = (cos(gs) * a->a[2] + a->a[0]) - (cos(ge) * b->a[2] + b->a[0]); dy = (sin(gs) * a->a[2] + a->a[1]) - (sin(ge) * b->a[2] + b->a[1]);
                        if (fabs(dx) < tol && fabs(dy) < tol) found = 1;
                    }
                }
            } else if (a->id == 'P') {
                if (fabs(a->a[0] - b->a[0]) < tol && fabs(a->a[1] - b->a[1]) < tol) found = 1;
            }
        }
        if (!found) { ok = 0; break; }
        int i22 = base + 1, i17 = cand - 1, i26 = cand + 1, i23 = R[top - 1];
        top--;
        if (i26 < i23) { L[top] = i26; R[top] = i23; top++; }
        if (i22 < i17) { L[top] = i22; R[top] = i17; top++; }
    }
    free(L); free(R);
    return ok;
}
/* FUN_18015e720 (l.188834-189080): the loop of the crossing records stack[from..] (each a section of s from its record to
   the next, FUN_180146e20); kept unless it is tiny (box diagonal under 1e-3) with its ends apart by more than a quarter of
   it; its direction FUN_1801270c0 */
static void so_loop_of(const IsisL *I, const int *stack, int ns, int from, const Sh *s, SL *out) {
    Sh *S = calloc(1, sizeof *S);
    int n = I->n;
    for (int k = from; k < ns; k++) {
        int i = stack[k], j = (i + 1) % n;
        sec_append_full(s, I->r[i].ent, I->r[i].t, I->r[j].ent, I->r[j].t, S, 0);
    }
    if (!S->n) { so_free(S); return; }
    P2 p0 = so_start(&S->e[0]), p1 = so_end(&S->e[S->n - 1]);
    double dg = sqrt((S->box[1] - S->box[3]) * (S->box[1] - S->box[3]) + (S->box[0] - S->box[2]) * (S->box[0] - S->box[2]));
    double gp = sqrt((p1.y - p0.y) * (p1.y - p0.y) + (p1.x - p0.x) * (p1.x - p0.x));
    if (gp <= dg * 0.25 || 0.001 <= dg) { double ar; S->dir = sh_orient_t(S, 0.001, &ar); S->nsrc = s->nsrc; S->unm = s->unm; S->srcisl = s->srcisl; sl_add(out, S); }   /* l.189030-189066: the source list of s */
    else so_free(S);
}
/* FUN_18015f790 (l.189378-189577): a ring traced along an undecided section: open -> clockwise; unless it is a retrace
   (FUN_180128bb0 1e-7), its self-crossings (FUN_18012f3b0 mode 0 = isis_build): none -> its direction; else the records in
   order on a stack, and whenever the next record's partner is on it, the loop from there (FUN_18015e720) cut off */
static void so_split_loops(SL *l) {
    Sh *sh = l->s[0];
    if (sh->dir == 0) sh->dir = -1;
    if (so_retrace(sh, 1e-07)) return;
    IsisL I = { 0 };
    isis_build(sh, 0, &I);
    if (!I.n) { double ar; sh->dir = sh_orient_t(sh, 0.001, &ar); free(I.r); return; }
    int *st = malloc((size_t)I.n * sizeof(int)), ns = 0;
    SL out = { 0 };
    for (int i = 0; i < I.n; i++) {
        st[ns++] = i;
        int want = I.r[(i + 1) % I.n].partner, k = ns - 1;
        while (k >= 0 && st[k] != want) k--;
        if (k >= 0) { so_loop_of(&I, st, ns, k, sh, &out); ns = k; }
    }
    free(st); free(I.r);
    if (out.n) { sl_clr(l); sl_move(&out, l); }
    free(out.s);
}
/* FUN_18015fda0 (l.189579-189823): the ring of one walk: the visits in order of their number, each run of consecutive
   numbers along A (ex[.][0]) or along B (ex[.][1]) a section copy FUN_180146e20; the records passed are marked 2 (or
   when the section to the next one is longer than 1e-5); a record still 0 (undecided) on the way needs FUN_18015f790 */
static void so_build(const Sh *A, const Sh *B, SoRec *r, int n, int (*ex)[2], int start, int lim, int *bad, SL *out) {
    int v0 = ex[start][0], v1 = ex[start][1], k;
    if (v0 == 0) { if (v1 == 0) return; k = v1; }
    else k = (v1 == 0 || v0 < v1) ? v0 : v1;
    Sh *sh = calloc(1, sizeof *sh);
    int und = 0, cur = start, ua = 0, ub = 0;
    while (k < lim) {
        if (ex[cur][0] == k) {
            do {
                int kk = cur; cur = (kk + 1) % n;
                if (r[kk].ca == 0) und = 1;
                if (r[kk].ca == 1 || !close_forward(A, r[kk].i.e_a, r[kk].i.t_a, r[cur].i.e_a, r[cur].i.t_a, 1e-05)) r[kk].ca = 2;
                k++;
            } while (ex[cur][0] == k);
            sec_append_full(A, r[start].i.e_a, r[start].i.t_a, r[cur].i.e_a, r[cur].i.t_a, sh, start == cur);
            ua = 1;
        } else if (ex[cur][1] == k) {
            do {
                int kk = cur; cur = r[kk].i.nxt_b;
                if (r[kk].cb == 0) und = 1;
                if (r[kk].cb == 1 || !close_forward(B, r[kk].i.e_b, r[kk].i.t_b, r[cur].i.e_b, r[cur].i.t_b, 1e-05)) r[kk].cb = 2;
                k++;
            } while (ex[cur][1] == k);
            sec_append_full(B, r[start].i.e_b, r[start].i.t_b, r[cur].i.e_b, r[cur].i.t_b, sh, start == cur);
            ub = 1;
        } else {
            int j = 0;
            while (j < n && ex[j][0] != k && ex[j][1] != k) j++;
            if (j >= n) { so_free(sh); return; }
            cur = j;
        }
        start = cur;
    }
    if (!sh->n) { so_free(sh); return; }
    /* l.189761-189800: the ring's source list CSh+0x78 = A's when a section of A was used (bVar3), then B's appended when one of B was (bVar4) */
    if (ua) { sh->nsrc = A->nsrc; sh->unm = A->unm; sh->srcisl = A->srcisl; }
    if (ub) { if (!sh->nsrc) sh->srcisl = B->srcisl; sh->nsrc += B->nsrc; sh->unm += B->unm; }
    if (und) {
        SL one = { 0 }; sl_add(&one, sh);
        so_split_loops(&one);   /* FUN_18015f790 */
        sl_move(&one, out); free(one.s);
        *bad = 1;
        return;
    } else {
        double ar; sh->dir = sh_orient_t(sh, 0.001, &ar);   /* FUN_1801270c0 (l.158722-158731) = FUN_180126ac0's direction */
    }
    sl_add(out, sh);
}
/* FUN_1801625f0 (l.191043-191092): step back over a joint of zero length (FUN_18014b720 1e-10) to a usable record */
static int so_back(const Sh *A, const Sh *B, const SoRec *r, int n, int *pcur, int *pside) {
    int i = *pcur, j, c;
    if (*pside == 0) {
        j = r[i].i.prv_b;
        if (!close_forward(B, r[j].i.e_b, r[j].i.t_b, r[i].i.e_b, r[i].i.t_b, 1e-10)) return 0;
        c = r[j].ca & 0xffff;
    } else {
        j = (i == 0 ? n : i) - 1;
        if (!close_forward(A, r[j].i.e_a, r[j].i.t_a, r[i].i.e_a, r[i].i.t_a, 1e-10)) return 0;
        c = r[j].cb & 0xffff;
    }
    if (c < 2) { *pcur = j; *pside = !*pside; return 1; }
    return 0;
}
/* FUN_1801627a0 (l.191095-191199): one leg of the walk: from *pcur on side *pside (0: along B by iNxtB, 1: along A by
   index) visit records (ex[.][1] resp. ex[.][0] = the visit number) while the pair (class on this side, class on the
   other) is one of 00 01 11 20 21 30 31; at another pair or a record visited already the side changes; true when the
   walk goes on from an unvisited record */
static int so_step(const Sh *A, const Sh *B, const SoRec *r, int n, int (*ex)[2], int *pcur, int *pcnt, int *pside) {
    int start = *pcur, side0 = *pside, i = start;
    for (;;) {
        int s = *pside;
        int hi = (s == 0 ? r[i].ca : r[i].cb) & 0xff, lo = (s == 0 ? r[i].cb : r[i].ca) & 0xff;
        int u = hi << 8 | lo;
        if (!(u == 0 || u == 1 || u == 0x101 || u == 0x200 || u == 0x201 || u == 0x300 || u == 0x301)) break;
        if (s == 0) ex[i][1] = *pcnt; else ex[i][0] = *pcnt;
        (*pcnt)++;
        i = s == 0 ? r[i].i.nxt_b : (i + 1) % n;
        if ((s == 0 ? ex[i][1] : ex[i][0]) != 0) break;
    }
    *pcur = i;
    int cs = *pside;
    *pside = !cs;
    if (ex[i][0] == 0 && ex[i][1] == 0) {
        int c = (cs == 0 ? r[i].ca : r[i].cb) & 0xffff;
        if (c < 2) return 1;
        if (so_back(A, B, r, n, pcur, pside) && (*pcur != start || *pside != side0)) return 1;
    }
    return 0;
}
/* FUN_180160b30 (l.190098-190122): the next record with a kept A section (side 1) or kept B section (side 0) */
static int so_next_start(const SoRec *r, int n, int *pi, int *pside) {
    int i = ++*pi;
    for (; i < n; i++, *pi = i) {
        if (r[i].ca == 1) { *pside = 1; break; }
        if (r[i].cb == 1) { *pside = 0; break; }
    }
    return *pi < n;
}
/* FUN_1801604e0 (l.189827-189888): DetNestLvlShAr, then only shapes whose direction fits their level kept (op 0: islands
   clockwise, outers counter-clockwise; else the reverse) */
static void so_keep_dir(int op, SL *l);
/* FUN_1801239f0 (l.156730-156740): the box is under tol in x and in y */
static int so_small(const Sh *s, double tol) { return s->box[2] - s->box[0] < tol && s->box[3] - s->box[1] < tol; }
/* FUN_18015eea0 (l.189084-189180), the default trace (bUseOldBuildSoFx 0): from every record starting a kept section
   one walk (FUN_1801627a0 until it stops), its ring FUN_18015fda0; then the gap closing FUN_18012c740(1e-10, 1e-10) */
static void so_trace(int op, const Sh *A, const Sh *B, SoRec *r, int n, SL *out) {
    SL tmp = { 0 };
    int (*ex)[2] = calloc((size_t)(n ? n : 1), sizeof *ex);
    int idx = -1, side = 0, bad = 0;
    while (so_next_start(r, n, &idx, &side)) {
        memset(ex, 0, (size_t)n * sizeof *ex);
        int cur = idx, cnt = 1;
        while (so_step(A, B, r, n, ex, &cur, &cnt, &side)) ;
        so_build(A, B, r, n, ex, cur, cnt, &bad, &tmp);
    }
    if (bad) {
        so_keep_dir(op, &tmp);
        for (int i = 0; i < tmp.n; i++) if (so_small(tmp.s[i], 1e-05)) { sl_del(&tmp, i); i--; }   /* FUN_180160650 (l.189891-189934) */
    }
    for (int i = 0; i < tmp.n; i++) { close_gaps(tmp.s[i], 1e-10); sh_box(tmp.s[i]); }   /* + GetMinMaxViaShEnt (l.161764) */
    sl_move(&tmp, out);
    free(tmp.s); free(ex);
}
/* FUN_180162010 (l.190783-191001): the boolean of two closed shapes, op 0 union, 1 a minus b, 2 intersection; the
   result rings appended to out. True when the boundaries cross (something appended) or the shapes are the same. */
static int so_core(int op, const Sh *a0, const Sh *b0, SL *out) {
    int ret = 0;
    if ((b0->box[2] <= a0->box[0] - 1e-07 && a0->box[0] - 1e-07 != b0->box[2]) || a0->box[2] + 1e-07 < b0->box[0] ||
        (b0->box[3] <= a0->box[1] - 1e-07 && a0->box[1] - 1e-07 != b0->box[3]) || a0->box[3] + 1e-07 < b0->box[1]) goto done;
    int before = out->n;
    Sh a, b; sh_copy(&a, a0); sh_copy(&b, b0);
    /* FUN_180161f60 (l.190733-190745): b turned to a's direction */
    if (a.dir != b.dir && (a.dir == 1 || a.dir == -1) && (b.dir == 1 || b.dir == -1)) sh_rev(&b);
    if (so_same(3.0000000000000004e-05, &a, &b) || so_on_each(3.0000000000000004e-05, &a, &b)) {
        if ((op & ~2) == 0) sl_add(out, so_dup(a0));
        ret = 1;
    } else {
        so_orient(op, &a, &b);
        int n = 0; Isab *L = isab_list(&a, &b, &n);   /* FUN_180133c70 */
        SoRec *r = calloc((size_t)(n ? n : 1), sizeof *r);
        for (int k = 0; k < n; k++) r[k].i = L[k];
        free(L);
        /* FUN_180160d00 (l.190146-190263): the class of each record's A section (to the next record) and B section (to
           iNxtB) from FUN_1801489a0 on the other shape (tol 1e-5): 4 outside / 2 inside -> kept 1 / dropped 3 by op;
           without both a kept and a dropped section the list is cleared */
        int any1 = 0, any3 = 0;
        for (int k = 0; k < n; k++) {
            int j = k < n - 1 ? k + 1 : 0, nb = r[k].i.nxt_b;
            int pa = sec_in_shape(&a, r[k].i.e_a, r[k].i.t_a, r[j].i.e_a, r[j].i.t_a, k == j, &b, 1e-05);
            int pb = sec_in_shape(&b, r[k].i.e_b, r[k].i.t_b, r[nb].i.e_b, r[nb].i.t_b, k == nb, &a, 1e-05);
            if (op == 0) { if (pa == 4) r[k].ca = 1; if (pb == 4) r[k].cb = 1; if (pa == 2) r[k].ca = 3; if (pb == 2) r[k].cb = 3; }
            else if (op == 1) { if (pa == 4) r[k].ca = 1; if (pb == 2) r[k].cb = 1; if (pa == 2) r[k].ca = 3; if (pb == 4) r[k].cb = 3; }
            else if (op == 2) { if (pa == 2) r[k].ca = 1; if (pb == 2) r[k].cb = 1; if (pa == 4) r[k].ca = 3; if (pb == 4) r[k].cb = 3; }
            if (r[k].ca == 1 || r[k].cb == 1) any1 = 1;
            if (r[k].ca == 3 || r[k].cb == 3) any3 = 1;
        }
        if (!(n > 0 && any1 && any3)) n = 0;
        /* FUN_180162520 (l.191005-191040): op 1 - some A section kept / some B section kept */
        int fa = 0, fb = 0;
        if (op == 1) for (int k = 0; k < n; k++) { if (r[k].ca == 1) fa = 1; if (r[k].cb == 1) fb = 1; }
        so_trace(op, &a, &b, r, n, out);
        free(r);
        if (op == 1 && before == out->n && fa) {
            if (fb) { ret = 1; goto freeab; }
            sl_add(out, so_dup(&a));   /* FUN_18011f2e0 of the turned copy */
        }
        ret = before < out->n;
    }
freeab:
    free(a.e); free(b.e);
done:
    if (g_so_dump) { char nm[32]; snprintf(nm, sizeof nm, "core%d_%d", op, ret); so_pr(nm, out); }
    return ret;
}
static void so_keep_dir(int op, SL *l) {
    int di = op == 0 ? -1 : 1, dout = op == 0 ? 1 : -1;
    so_nest(l);
    for (int i = 0; i < l->n; i++) if (l->s[i]->dir != (so_isl(l->s[i]) ? di : dout)) { sl_del(l, i); i--; }
}
/* FUN_180163400 / FUN_180163a90 / FUN_18015dc60 (l.191579, 191819, 188488): the core, then every shape of out (all of
   it) whose box is under tol removed */
static int so_core_tol(int op, double tol, const Sh *a, const Sh *b, SL *out) {
    int r = so_core(op, a, b, out);
    for (int i = 0; i < out->n; i++) if (so_small(out->s[i], tol)) { sl_del(out, i); i--; }
    return r;
}
/* FUN_1801638f0 (l.191755-191769) + FUN_180163580 (l.191627-191752): intersection of two shapes; no crossing: a inside
   b -> a (4), b inside a -> b (2), else nothing (3) */
static int so_isect2(double tol, const Sh *a, const Sh *b, SL *out) {
    if (so_core_tol(2, tol, a, b, out)) return 1;
    int pa, pb; shape_in_shape(a, b, &pa, &pb);
    if (pb < pa) { sl_add(out, so_dup(a)); return 4; }
    if (pa < pb) { sl_add(out, so_dup(b)); return 2; }
    return 3;
}
/* FUN_180163a20 (l.191802-191816) + FUN_180163bf0 (l.191867-192000): union of two shapes; no crossing: a inside b -> b
   (4), b inside a -> a (2), else a and b (3) */
static int so_union2(double tol, const Sh *a, const Sh *b, SL *out) {
    if (so_core_tol(0, tol, a, b, out)) return 1;
    int pa, pb; shape_in_shape(a, b, &pa, &pb);
    if (pb < pa) { sl_add(out, so_dup(b)); return 4; }
    sl_add(out, so_dup(a));
    if (pa < pb) return 2;
    sl_add(out, so_dup(b));
    return 3;
}
/* FUN_18015e180 (l.188666-188680) + FUN_18015dde0 (l.188536-188662): a minus b; no crossing: a inside b -> nothing (4),
   b inside a -> b then a (2), else a (3) */
static int so_minus2(double tol, const Sh *a, const Sh *b, SL *out) {
    if (so_core_tol(1, tol, a, b, out)) return 1;
    int pa, pb; shape_in_shape(a, b, &pa, &pb);
    if (pb < pa) return 4;
    if (pa < pb) sl_add(out, so_dup(b));
    sl_add(out, so_dup(a));
    return pa < pb ? 2 : 3;
}
static int so_union_code(double tol, const Sh *a, const Sh *b) {   /* FUN_180163960 (l.191772-191799): only the answer */
    SL t = { 0 }; int r = so_union2(tol, a, b, &t); sl_fre(&t); return r;
}
/* FUN_18015c370 (l.187619-187682): nothing to intersect - the outers apart (union 3), the outer of Y inside an island of
   X (2) or the outer of X inside an island of Y (4) */
static int so_shortcut(double tol, const SL *X, const SL *Y) {
    const Sh *ox = X->s[X->n - 1], *oy = Y->s[Y->n - 1];
    if (so_union_code(tol, ox, oy) == 3) return 1;
    for (int i = 0; i < X->n - 1; i++) if (so_union_code(tol, X->s[i], oy) == 2) return 1;
    for (int i = 0; i < Y->n - 1; i++) if (so_union_code(tol, ox, Y->s[i]) == 4) return 1;
    return 0;
}
/* FUN_18015aac0 (l.186771-186838): L = the intersection of the outers; when not empty, copies of X's then Y's islands first */
static void so_aac0(double tol, const SL *X, const SL *Y, SL *L) {
    sl_clr(L);
    SL t = { 0 };
    so_isect2(tol, X->s[X->n - 1], Y->s[Y->n - 1], &t);
    if (t.n) {
        for (int i = 0; i < X->n - 1; i++) sl_add(L, so_dup(X->s[i]));
        for (int i = 0; i < Y->n - 1; i++) sl_add(L, so_dup(Y->s[i]));
        sl_move(&t, L);
    }
    free(t.s);
}
/* mcce::GetMinMaxViaSh (l.11473-11556) */
static void so_list_box(const SL *l, double mn[2], double mx[2]) {
    if (!l->n) return;
    mn[0] = l->s[0]->box[0]; mn[1] = l->s[0]->box[1]; mx[0] = l->s[0]->box[2]; mx[1] = l->s[0]->box[3];
    for (int i = 1; i < l->n; i++) {
        const double *b = l->s[i]->box;
        if (b[0] < mn[0]) mn[0] = b[0];
        if (b[1] < mn[1]) mn[1] = b[1];
        if (mx[0] < b[2]) mx[0] = b[2];
        if (mx[1] < b[3]) mx[1] = b[3];
    }
}
/* FUN_180124c50 (l.157422-157515): the rectangle mn..mx, four lines (x0 y0)-(x0 y1)-(x1 y1)-(x1 y0)-back, clockwise, outer */
static Sh *so_rect(const double mn[2], const double mx[2]) {
    Sh *s = calloc(1, sizeof *s);
    double x[4] = { mn[0], mn[0], mx[0], mx[0] }, y[4] = { mn[1], mx[1], mx[1], mn[1] };
    for (int i = 0; i < 4; i++) {
        Ent e; memset(&e, 0, sizeof e); e.id = 'L'; e.src_sh = e.src_ent = -1;
        e.a[0] = x[i]; e.a[1] = y[i]; e.a[2] = x[(i + 1) & 3]; e.a[3] = y[(i + 1) & 3];
        ent_box(&e); sh_push(s, &e);
    }
    s->box[0] = mn[0]; s->box[1] = mn[1]; s->box[2] = mx[0]; s->box[3] = mx[1];
    s->dir = -1; s->isl = 0;
    return s;
}
static void so_union_ar(double tol, const SL *in, SL *out);
/* FUN_180159c60 (l.186238-186549): two or more leading islands: they are united (as outers, SoPockUnionAr), nested
   inside a rectangle round them grown by 1 (DetNestLvlShAr), the rectangle dropped; the rest of the list after them */
static void so_c60(double tol, SL *L) {
    int k = 0;
    while (k < L->n && so_isl(L->s[k]) && k <= 1) k++;
    if (k < 2) return;
    SL I = { 0 }, U = { 0 };
    while (L->n && so_isl(L->s[0])) { L->s[0]->isl = 0; sl_add(&I, L->s[0]); sl_pop0(L); }
    so_union_ar(tol, &I, &U);
    sl_fre(&I);
    double mn[2] = { 0, 0 }, mx[2] = { 0, 0 };
    so_list_box(&U, mn, mx);
    mn[0] += -1.0; mn[1] += -1.0; mx[0] += 1.0; mx[1] += 1.0;
    sl_add(&U, so_rect(mn, mx));
    so_nest(&U);
    sl_del(&U, U.n - 1);
    sl_move(L, &U);
    free(L->s); *L = U;
}
/* FUN_18015a4f0 (l.186553-186768): when a non-island stands before the last run of islands, every non-island before the
   trailing non-islands is intersected with each of them (its islands before it dropped when that gives nothing); the
   islands and the trailing shapes are carried over */
static void so_a4f0(double tol, SL *L) {
    int n = L->n, i12 = n - 1;
    while (i12 >= 0 && !so_isl(L->s[i12])) i12--;
    int i13 = i12 + 1;
    if (!(i13 > 0 && i12 >= 0)) return;
    while (i12 >= 0 && so_isl(L->s[i12])) i12--;
    if (i12 < 0) return;
    SL out = { 0 };
    for (int k = 0; k < L->n; k++) {
        if (!so_isl(L->s[k]) && k < i13) {
            int before = out.n;
            for (int j = i13; j < L->n; j++) so_isect2(tol, L->s[k], L->s[j], &out);
            if (out.n == before) while (out.n > 0 && so_isl(out.s[out.n - 1])) sl_del(&out, out.n - 1);
        } else { sl_add(&out, L->s[k]); L->s[k] = NULL; }
    }
    sl_fre(L); *L = out;
}
/* FUN_180159230 (l.185859-186235): group by group (leading islands, then the non-islands after them) every non-island
   minus each island (FUN_18015e180), islands among them copied as they are */
static void so_9230(double tol, SL *L) {
    int k = 0;
    while (k < L->n && so_isl(L->s[k])) k++;
    if (k >= L->n) return;
    SL out = { 0 };
    while (L->n) {
        SL I = { 0 }, O = { 0 };
        while (L->n && so_isl(L->s[0])) { sl_add(&I, L->s[0]); sl_pop0(L); }
        while (L->n && !so_isl(L->s[0])) { sl_add(&O, L->s[0]); sl_pop0(L); }
        for (int i = 0; i < I.n; i++) {
            SL nw = { 0 };
            for (int j = 0; j < O.n; j++) {
                if (so_isl(O.s[j])) sl_add(&nw, so_dup(O.s[j]));
                else so_minus2(tol, O.s[j], I.s[i], &nw);
            }
            sl_fre(&O); O = nw;
        }
        sl_move(&O, &out);
        sl_fre(&O); sl_fre(&I);
    }
    sl_fre(L); *L = out;
}
/* FUN_18015b2e0 (l.187022-187082): two region groups (islands..., outer): when the outers' boxes overlap (1.19e-7) and
   FUN_18015c370 does not rule it out, FUN_18015aac0, FUN_180159c60, FUN_18015a4f0, FUN_180159230, DetNestLvlShAr */
static void so_pairwise(double tol, const SL *X, const SL *Y, SL *out) {
    if (!X->n || !Y->n) return;
    const double *bx = X->s[X->n - 1]->box, *by = Y->s[Y->n - 1]->box, e = 1.1920928955078125e-07;
    if (!(bx[0] - e <= by[2] && by[0] <= bx[2] + e && bx[1] - e <= by[3] && by[1] <= bx[3] + e)) return;
    if (so_shortcut(tol, X, Y)) return;
    SL L = { 0 };
    so_aac0(tol, X, Y, &L); so_pr("aac0", &L);
    so_c60(tol, &L); so_pr("c60", &L);
    so_a4f0(tol, &L); so_pr("a4f0", &L);
    so_9230(tol, &L); so_pr("9230", &L);
    so_nest(&L); so_pr("pw", &L);
    sl_move(&L, out);
    free(L.s);
}
/* FUN_18010f0e0 (l.146191-146218): the area of a list (FUN_180126ac0 tol 0.1, islands negative), at least 0 */
static double so_area(Sh *const *s, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++) { double a = 0.0; sh_orient_t(s[i], 0.1, &a); if (so_isl(s[i])) a = -a; sum += a; }
    return 0.0 <= sum ? sum : 0.0;
}
/* FUN_1801523f0 (l.181792-182019): groups whose area is under tol^2 removed */
static void so_drop_small(double tol, SL *L) {
    int i = 0;
    while (i < L->n) {
        int j = i;
        while (j < L->n && so_isl(L->s[j])) j++;
        int cnt = j - i + (j < L->n ? 1 : 0);
        if (so_area(L->s + i, cnt) < tol * tol) { for (int k = 0; k < cnt; k++) sl_del(L, i); }
        else i += cnt;
    }
}
/* FUN_18013c2c0 (l.169798-169840): the box of a list, onto mn / mx */
static void so_box_onto(const SL *l, double mn[2], double mx[2]) {
    for (int i = 0; i < l->n; i++) {
        const double *b = l->s[i]->box;
        mn[0] = mn[0] <= b[0] ? mn[0] : b[0]; mn[1] = mn[1] <= b[1] ? mn[1] : b[1];
        mx[0] = b[2] <= mx[0] ? mx[0] : b[2]; mx[1] = b[3] <= mx[1] ? mx[1] : b[3];
    }
}
/* FUN_180153390 (l.182330-182599): group X minus group Y = X with each island of Y (taken as an outer) plus X with the
   rectangle round both (grown by 10) minus Y's outer (the outer as its island); DetNestLvlShAr, FUN_1801523f0 */
static void so_minus_group(double tol, const SL *X, const SL *Y, SL *out) {
    double mn[2] = { 3.4028234663852886e+38, 3.4028234663852886e+38 }, mx[2] = { -3.4028234663852886e+38, -3.4028234663852886e+38 };
    so_box_onto(X, mn, mx); so_box_onto(Y, mn, mx);
    mn[0] += -10.0; mn[1] += -10.0; mx[0] += 10.0; mx[1] += 10.0;
    SL R = { 0 };
    if (Y->n > 1) for (int i = 0; i < Y->n - 1; i++) {
        SL g = { 0 }; Sh *c = so_dup(Y->s[i]); c->isl = 0; sl_add(&g, c);
        so_pairwise(tol, X, &g, &R);
        sl_fre(&g);
    }
    SL g = { 0 }; Sh *c = so_dup(Y->s[Y->n - 1]); c->isl = 1; sl_add(&g, c); sl_add(&g, so_rect(mn, mx));
    so_pairwise(tol, X, &g, &R);
    sl_fre(&g);
    so_nest(&R);
    so_drop_small(tol, &R);
    sl_move(&R, out);
    free(R.s);
}
/* FUN_180153b30 (l.182601-183001): X minus Y group by group of Y: every group of the current result minus it
   (FUN_180153390), DetNestLvlShAr between */
static void so_minus(double tol, const SL *X, const SL *Y, SL *out) {
    SL cur = { 0 }; sl_copy_into(X, &cur);
    int i = 0;
    while (i < Y->n) {
        SL Yg = { 0 };
        while (i < Y->n && so_isl(Y->s[i])) sl_add(&Yg, Y->s[i++]);
        if (i < Y->n) sl_add(&Yg, Y->s[i]);
        SL nw = { 0 };
        for (int j = 0; j < cur.n; ) {
            SL Xg = { 0 };
            while (j < cur.n && so_isl(cur.s[j])) sl_add(&Xg, cur.s[j++]);
            if (j < cur.n) sl_add(&Xg, cur.s[j++]);
            so_minus_group(tol, &Xg, &Yg, &nw);
            free(Xg.s);
        }
        free(Yg.s);
        sl_clr(&cur);
        sl_move(&nw, &cur); free(nw.s);
        if (!cur.n) break;
        so_nest(&cur);
        i++;
    }
    sl_move(&cur, out); free(cur.s);
}
/* FUN_18015c940 (l.187808-187866): the union of two groups: single shapes FUN_180163a20; otherwise unless FUN_18015c370
   rules it out the outers' union, the islands' pairwise intersections, each island of A minus B's outer and each of B
   minus A's outer (else both copied); DetNestLvlShAr */
static void so_union_groups(double tol, const SL *A, const SL *B, SL *R) {
    SL t = { 0 };
    if (A->n < 2 && B->n < 2) so_union2(tol, A->s[0], B->s[0], &t);
    else if (!so_shortcut(tol, A, B)) {
        so_union2(tol, A->s[A->n - 1], B->s[B->n - 1], &t);
        for (int i = 0; i < A->n - 1; i++) for (int j = 0; j < B->n - 1; j++) so_isect2(tol, A->s[i], B->s[j], &t);   /* FUN_18015c5e0 */
        for (int i = 0; i < A->n - 1; i++) so_minus2(tol, A->s[i], B->s[B->n - 1], &t);   /* FUN_18015c520 */
        for (int i = 0; i < B->n - 1; i++) so_minus2(tol, B->s[i], A->s[A->n - 1], &t);
    } else { sl_copy_into(A, &t); sl_copy_into(B, &t); }
    so_nest(&t);
    sl_move(&t, R); free(t.s);
}
/* mcce::SoPockUnionAr (l.2138-2760): group A against each later group B; a union with other than exactly two outers
   replaces both groups by its first group (the rest of it dropped) and the scan goes on after it; DetNestLvlShAr */
static void so_union_ar(double tol, const SL *in, SL *out) {
    SL L = { 0 }; sl_copy_into(in, &L);
    int i24 = 0;
    while (i24 < L.n) {
        SL A = { 0 };
        while (so_isl(L.s[i24])) sl_add(&A, L.s[i24++]);
        sl_add(&A, L.s[i24]);
        int i23 = i24 + 1;
        while (i23 < L.n) {
            SL B = { 0 }, R = { 0 };
            while (so_isl(L.s[i23])) sl_add(&B, L.s[i23++]);
            sl_add(&B, L.s[i23]);
            so_union_groups(tol, &A, &B, &R);
            free(B.s);
            int nout = 0;
            for (int k = 0; k < R.n; k++) if (!so_isl(R.s[k])) nout++;
            if (R.n >= 1 && nout == 2) { sl_fre(&R); i23++; continue; }
            sl_del(&L, i23);
            i23--;
            while (i23 >= 0 && so_isl(L.s[i23])) { sl_del(&L, i23); i23--; }
            sl_del(&L, i24);
            while (i24 - 1 >= 0 && so_isl(L.s[i24 - 1])) { sl_del(&L, i24 - 1); i24--; }
            i24--;
            A.n = 0;
            for (int k = 0; k < R.n; k++) {
                Sh *s = R.s[k]; R.s[k] = NULL;
                sl_add(&A, s); i24++;
                sl_ins(&L, i24, s);
                if (!so_isl(s)) break;
            }
            sl_fre(&R);
            i23 = i24 + 1;
        }
        free(A.s);
        i24++;
    }
    so_nest(&L);
    sl_move(&L, out); free(L.s);
}

/* ---- API (mcce_ctour.h) ---- */
static void so_in(const McceShape *in, int n, SL *l) {
    for (int i = 0; i < n; i++) {
        Sh *s = calloc(1, sizeof *s);
        for (int k = 0; k < in[i].n; k++) { Ent e = in[i].e[k]; ent_box(&e); sh_push(s, &e); }
        s->dir = in[i].dir; s->isl = in[i].isl;
        sh_box(s);
        sl_add(l, s);
    }
}
McceResult mcce_region_bool(int op, double tol, const McceShape *x, int nx, const McceShape *y, int ny, void *dump) {
    SL X = { 0 }, Y = { 0 }, O = { 0 };
    g_so_refused = NULL; g_so_dump = dump;
    so_in(x, nx, &X); so_in(y, ny, &Y);
    so_pr("inX", &X); so_pr("inY", &Y);
    if (op == MCCE_RB_MINUS) so_minus(tol, &X, &Y, &O);
    else if (op == MCCE_RB_ISECT) so_pairwise(tol, &X, &Y, &O);
    else so_union_ar(tol, &X, &O);
    so_pr("result", &O);
    sl_fre(&X); sl_fre(&Y);
    g_so_dump = NULL;
    McceResult r; memset(&r, 0, sizeof r);
    if (g_so_refused) { r.refused = g_so_refused; sl_fre(&O); return r; }
    for (int i = 0; i < O.n; i++) { Sh *s = O.s[i]; out_push(&r, s->e, s->n, s->dir, s->isl); s->e = NULL; }
    sl_fre(&O);
    return r;
}
/* the unit oracle's input (tools/peloader/mcce_sobool.c: "SOBOOL 1", op, tol, X n / Y n shapes) */
static int so_read_list(FILE *f, int n, McceShape **out) {
    char line[512];
    *out = calloc((size_t)(n ? n : 1), sizeof(McceShape));
    for (int i = 0; i < n; i++) {
        int dir, isl, m;
        if (!fgets(line, sizeof line, f) || sscanf(line, "s %d %d %d", &dir, &isl, &m) != 3) return -1;
        McceShape *s = &(*out)[i];
        s->e = calloc((size_t)(m ? m : 1), sizeof(McceEnt)); s->n = m; s->dir = dir == 1 ? 1 : -1; s->isl = isl != 0;   /* CopyPockBndsToShAr: dir = ccw ? 1 : -1 */
        for (int k = 0; k < m; k++) {
            if (!fgets(line, sizeof line, f)) return -1;
            char *p = line; while (*p == ' ') p++;
            McceEnt *e = &s->e[k]; e->id = *p++; e->src_sh = i; e->src_ent = k;
            for (int c = 0; c < (e->id == 'A' ? 5 : 4); c++) e->a[c] = strtod(p, &p);
        }
    }
    return 0;
}
int mcce_region_bool_file(const char *in, const char *outp) {
    FILE *f = fopen(in, "r");
    if (!f) return 2;
    char line[512], op[16] = ""; double tol = 1e-6; int nx = 0, ny = 0; McceShape *X = NULL, *Y = NULL; int rc = 0;
    if (!fgets(line, sizeof line, f) || strncmp(line, "SOBOOL 1", 8)) { fclose(f); return 2; }
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "end", 3)) break;
        if (!strncmp(line, "op ", 3)) sscanf(line + 3, "%15s", op);
        else if (!strncmp(line, "tol ", 4)) tol = strtod(line + 4, NULL);
        else if (!strncmp(line, "X ", 2)) { nx = atoi(line + 2); if (so_read_list(f, nx, &X)) rc = 2; }
        else if (!strncmp(line, "Y ", 2)) { ny = atoi(line + 2); if (so_read_list(f, ny, &Y)) rc = 2; }
    }
    fclose(f);
    FILE *o = rc ? NULL : fopen(outp, "w");
    if (o) {
        int k = !strcmp(op, "minus") ? MCCE_RB_MINUS : !strcmp(op, "isect") ? MCCE_RB_ISECT : MCCE_RB_UNION;
        McceResult r = mcce_region_bool(k, tol, X, nx, Y, ny, o);
        if (r.refused) fprintf(o, "# refused: %s\n", r.refused);
        mcce_shapes_free(&r);
        fclose(o);
    } else rc = 2;
    for (int i = 0; i < nx; i++) free(X[i].e);
    for (int i = 0; i < ny; i++) free(Y[i].e);
    free(X); free(Y);
    return rc;
}
