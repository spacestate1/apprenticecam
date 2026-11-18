/* mcce_medent.c - port of the Dynamic Mill (mcce MCCECalcDynPock) plunge-entry methods that use the medial axis
 * (Mastercam 2017 mcce.dll), on top of the CalcMedAx port (mcce_medax.c / mcce_medax_par.c). Included by mcce_dyn.c just
 * before plunge_entry (uses its shapes, offsets, helix / ramp builders).
 *
 * Spec: decomp/medax/dynentry/dynentry_spec.txt ("DE x.y" below) and troch_walk_spec.txt ("TW x.y");
 * "l.N" = decomp/mcce_all.c line N, "f.A-B" = function body, "dis 0x..." = objdump of mcce.dll. Every constant, comparison
 * strictness and loop order follows the decompile; anything that is ours is marked "ours". */

/* ============================================================================================ medial chain machinery (DE 3) */
/* FUN_1800c4f50 (f.106434-106473): start point (types 0/2 O + 0 d; type 1 (+0x90) (h u0^2, 2 h u0)) */
static P2 mae_start(const Mae *r) {
    if ((r->type & ~2) == 0) return p2(0.0 * r->d.x + r->O.x, 0.0 * r->d.y + r->O.y);
    if (r->type == 1) return mx_apply(r->Mi, p2(r->u0 * r->h * r->u0, r->u0 * r->h * 2.0));
    return p2(0, 0);
}
/* FUN_1800c47c0 (f.106198-106217): both children absent or pruned */
static int mae_end_leaf(const MaeL *M, int i) {
    int a = M->r[i].ch1, b = M->r[i].ch2;
    return (a == -1 || (M->r[a].flags & 2)) && (b == -1 || (M->r[b].flags & 2));
}
/* FUN_1800c4830 (f.106221-106235): parent absent or pruned */
static int mae_root_leaf(const MaeL *M, int i) { int p = M->r[i].parent; return p == -1 || (M->r[p].flags & 2); }
/* FUN_1800c4170 (f.106001-106075): the free end point coincides (|d|^2 < 1e-12) with an end of another live edge */
static int mae_free_end_touches(const MaeL *M, int i, int endLeaf, int rootLeaf) {
    P2 P;
    if (endLeaf) P = mae_end(&M->r[i]);
    else if (rootLeaf) P = mae_start(&M->r[i]);
    else return 1;
    for (int j = 0; j < M->n; j++) {
        if ((M->r[j].flags & 2) || j == i) continue;
        P2 a = mae_start(&M->r[j]), b = mae_end(&M->r[j]);
        if ((P.x - a.x) * (P.x - a.x) + (P.y - a.y) * (P.y - a.y) < 1e-12 || (P.y - b.y) * (P.y - b.y) + (P.x - b.x) * (P.x - b.x) < 1e-12) return 1;
    }
    return 0;
}
/* single wrap of an angle difference into [-pi, pi] (l.106766-106784: d < -pi -> += 2pi, else d > pi -> -= 2pi) */
static double wrap1(double d) { if (d < -3.141592653589793) d += 6.283185307179586; else if (3.141592653589793 < d) d -= 6.283185307179586; return d; }
/* FUN_1800c5690 (f.106697-106890, DE 3.2): the angle subtended at the edge by its two boundary sites; 0 when degenerate */
static int mae_subtended(const MxSL *S, const Mae *r, double *a) {
    *a = 0.0;
    P2 A, B;
    if (r->type == 0) {
        const MxE *e1 = G_ent(S, r->sh1, r->en1), *e2 = G_ent(S, r->sh2, r->en2);
        P2 d1 = psub(e1->p2, e1->p1), d2 = psub(e2->p2, e2->p1);
        if (fabs(d2.x) <= 1e-12 && fabs(d2.y) <= 1e-12) return 0;
        double t2 = atan2(d2.y, d2.x);
        if (fabs(d1.x) <= 1e-12 && fabs(d1.y) <= 1e-12) return 0;
        *a = fabs(wrap1(t2 - atan2(d1.y, d1.x)));                              /* l.106766-106784, l.106886 */
        return 1;
    } else if (r->type == 1) {                                                  /* l.106785-106812 */
        double u = 0.0, h = r->h;
        if (0.0 <= r->u0 || r->u1 <= 0.0) u = (fabs(r->u1) <= fabs(r->u0)) ? r->u1 : r->u0;
        double hu = h * u, h2u = hu * 2.0, hu2 = hu * u;
        A = p2(hu2 - -h, h2u - h2u);
        B = p2(h - hu2, 0.0 - h2u);
    } else if (r->type == 2) {                                                  /* l.106814-106857 */
        P2 p1 = G_ent(S, r->sh1, r->en1)->p1, q = G_ent(S, r->sh2, r->en2)->p1, d = psub(q, p1); double t;
        if (!lnln(r->O, r->d, p1, d, &t, NULL, NULL)) return 0;
        double s = 0.0; if (0.0 <= t) s = (t <= r->s1) ? t : r->s1;
        P2 X = p2(s * r->d.x + r->O.x, s * r->d.y + r->O.y);
        A = psub(X, p1); B = p2((d.x + p1.x) - X.x, (d.y + p1.y) - X.y);
    } else return 0;
    if (fabs(A.x) <= 1e-12 && fabs(A.y) <= 1e-12) return 0;                    /* l.106863-106865 */
    double ta = atan2(A.y, A.x);
    if (fabs(B.x) <= 1e-12 && fabs(B.y) <= 1e-12) return 0;
    *a = fabs(3.141592653589793 - fabs(wrap1(ta - atan2(B.y, B.x))));          /* l.106864-106886 */
    return 1;
}
/* FUN_1800c4140 = FUN_1800c43e0 (links to pruned edges := -1, f.106076-106121) + FUN_1800c3c10 (erase pruned, renumber,
   f.105833-105977) - the same steps as the tail of post_process (mcce_medax.c) */
static void mae_compact(MaeL *out) {
    for (int i = 0; i < out->n; i++) {
        Mae *m = &out->r[i]; if (m->flags & 2) continue;
        if (m->parent != -1 && (out->r[m->parent].flags & 2)) m->parent = -1;
        if (m->ch1 != -1 && (out->r[m->ch1].flags & 2)) m->ch1 = -1;
        if (m->ch2 != -1 && (out->r[m->ch2].flags & 2)) m->ch2 = -1;
    }
    int *map = malloc((out->n ? out->n : 1) * sizeof *map), n = 0;
    for (int i = 0; i < out->n; i++) if (!(out->r[i].flags & 2)) { map[i] = n; out->r[n++] = out->r[i]; }
    out->n = n;
    for (int i = 0; i < n; i++) {
        Mae *m = &out->r[i];
        if (m->parent != -1) m->parent = map[m->parent];
        if (m->ch1 != -1) m->ch1 = map[m->ch1];
        if (m->ch2 != -1) m->ch2 = map[m->ch2];
    }
    free(map);
}
/* FUN_1800c4870 (f.106239-106296, DE 3.1): prune leaf branches whose two sites lie on the same boundary shape and subtend
   <= 164 deg, until nothing changes; all pruned -> keep the max-clearance vertex (FUN_1800c3a90); compact */
static void mae_prune(const MxSL *S, MaeL *M) {
    int changed;
    do {
        changed = 0;
        for (int i = 0; i < M->n; i++) {
            if (M->r[i].flags & 6) continue;                                    /* l.106268 */
            int endLeaf = mae_end_leaf(M, i), rootLeaf = mae_root_leaf(M, i);
            if ((endLeaf || rootLeaf) && M->r[i].sh1 == M->r[i].sh2 && !mae_free_end_touches(M, i, endLeaf, rootLeaf)) {
                double a; mae_subtended(S, &M->r[i], &a);                       /* return value ignored (l.106277-106279) */
                if (a <= 2.8623399732707004) { M->r[i].flags |= 2; changed = 1; }   /* l.106280-106284 */
            }
        }
    } while (changed);
    int k = 0;                                                                  /* FUN_1800c3a90 (f.105789-105832) */
    while (k < M->n && (M->r[k].flags & 2)) k++;
    if (M->n <= k && k != 0) {
        double s; int bi = max_clear(S, M, &s, NULL);
        if (bi >= 0 && bi < M->n) { mae_trim(S, &M->r[bi], s, s); M->r[bi].flags &= ~2u; }
    }
    mae_compact(M);
}

/* FUN_1800a5f00 (f.90569-90758, DE 3.4.1): the parabola arc u0..u1 as a polyline by adaptive midpoint subdivision of its
   quadratic Bezier (FUN_1800abf20 f.93426-93460 control points), points from u0 to u1, start included */
static void par_polyline(double tol, double h, double u0, double u1, const double Mx[6], P2 **pts, int *np, int *cap) {
    P2 Q0 = p2(h * u0 * u0, h * 2.0 * u0), Q2 = p2(h * u1 * u1, h * 2.0 * u1), Q1;
    if (!lnln(Q0, p2(h * 2.0 * u0, h * 2.0), Q2, p2(h * 2.0 * u1, h * 2.0), NULL, NULL, &Q1)) Q1 = Q0;
    Q0 = mx_apply(Mx, Q0); Q1 = mx_apply(Mx, Q1); Q2 = mx_apply(Mx, Q2);   /* dis 0x1800a5f74-0x1800a5fb9 */
#define PUSH(q) do { if (*np == *cap) { *cap = *cap ? 2 * *cap : 16; *pts = realloc(*pts, *cap * sizeof **pts); } (*pts)[(*np)++] = (q); } while (0)
    PUSH(Q0);                                                                   /* withStart (l.90634-90635) */
    int sn = 3, sc = 64; P2 *st = malloc(sc * sizeof *st);
    st[0] = Q2; st[1] = Q1; st[2] = Q0;                                         /* l.90642-90644 */
    long guard = 0;
    do {
        if (sn + 2 > sc) { sc *= 2; st = realloc(st, sc * sizeof *st); }
        P2 Cold = st[sn - 2], S = st[sn - 1], E = st[sn - 3];
        st[sn + 1] = S;
        st[sn - 2] = p2((Cold.x + E.x) * 0.5, (Cold.y + E.y) * 0.5);
        st[sn] = p2((Cold.x + S.x) * 0.5, (Cold.y + S.y) * 0.5);
        st[sn - 1] = p2((st[sn - 2].x + st[sn].x) * 0.5, (st[sn - 2].y + st[sn].y) * 0.5);
        P2 Mid = st[sn - 1];
        if ((Cold.x - Mid.x) * (Cold.x - Mid.x) + (Cold.y - Mid.y) * (Cold.y - Mid.y) < tol * tol) {   /* l.90685-90686 */
            PUSH(Mid); PUSH(E);
            sn -= 2;
        } else sn += 2;
        if (++guard > 4000000) break;                                           /* ours: runaway guard */
    } while (sn != 1);                                                          /* l.90756 */
#undef PUSH
    free(st);
}
/* FUN_1800c2270 (f.105132-105476, DE 3.4): append the edge's polyline to sh, forward (back = 0) or backward; every new
   segment 'L' carries +0x0c = edge index (ref_ent) and +0x13 (b13) = forward: 2 on a root edge; backward: 1 | (leaf ? 2 : 0) */
static void chain_emit(int back, int i, const MaeL *M, double tolA, double tolB, Sh *sh) {
    const Mae *e = &M->r[i];
    int leaf = e->ch1 == -1 && e->ch2 == -1, par = e->parent;                   /* l.105206-105210 */
    P2 *pts = NULL; int np = 0, cap = 0;
    if ((e->type & ~2) == 0) {                                                  /* l.105213-105250 */
        pts = malloc(2 * sizeof *pts); np = 2;
        pts[0] = e->O; pts[1] = p2(e->s1 * e->d.x + e->O.x, e->s1 * e->d.y + e->O.y);
    } else if (e->type == 1) par_polyline(tolA, e->h, e->u0, e->u1, e->Mi, &pts, &np, &cap);   /* l.105251-105268 */
    if (np == 0) { free(pts); return; }
    P2 cur = sh->n == 0 ? (back ? pts[np - 1] : pts[0]) : e_end(&sh->e[sh->n - 1]);
    if (!back) {                                                                /* l.105401-105450 */
        for (int k = 1; k < np; k++) {
            double dx = cur.x - pts[k].x, dy = cur.y - pts[k].y;
            if (tolB * tolB < dx * dx + dy * dy) {
                Ent l = ent_line(cur, pts[k]); l.ref_ent = i; l.b13 = par == -1 ? 2 : 0;
                sh_push(sh, l); cur = pts[k];
            }
        }
    } else {                                                                    /* l.105273-105366 */
        for (int k = np - 2; k >= 0; k--) {
            double dx = cur.x - pts[k].x, dy = cur.y - pts[k].y;
            if (tolB * tolB < dx * dx + dy * dy) {
                Ent l = ent_line(cur, pts[k]); l.ref_ent = i; l.b13 = 1 | (leaf ? 2 : 0);
                sh_push(sh, l); cur = pts[k];
            }
        }
    }
    free(pts);
}
/* FUN_1800c2c20 (f.105477-105495): Euler tour of the subtree */
static void chain_walk(int i, const MaeL *M, double tolA, double tolB, Sh *sh, int depth) {
    if (depth > 100000) return;                                                 /* ours: recursion guard */
    chain_emit(0, i, M, tolA, tolB, sh);
    if (M->r[i].ch1 != -1) chain_walk(M->r[i].ch1, M, tolA, tolB, sh, depth + 1);
    if (M->r[i].ch2 != -1) chain_walk(M->r[i].ch2, M, tolA, tolB, sh, depth + 1);
    chain_emit(1, i, M, tolA, tolB, sh);
}
/* FUN_1800c1c90 (f.104909-105131, DE 3.5): join the tour whose first entity starts at the same point (nearest later one,
   strict <, d^2 < 1.0000000000000002e-10) to each tour */
static void chains_merge(ShL *out) {
    for (int i = 0; i < out->n; i++) {
        if (!out->s[i]->n) continue;                                            /* (ours: an empty shape has no ent[0]) */
        P2 P = e_start(&out->s[i]->e[0]);
        double best = 3.4028234663852886e38; int jb = -1;
        for (int j = i + 1; j < out->n; j++) {
            if (!out->s[j]->n) continue;
            P2 Q = e_start(&out->s[j]->e[0]); double d2 = (P.x - Q.x) * (P.x - Q.x) + (P.y - Q.y) * (P.y - Q.y);
            if (d2 < best) { best = d2; jb = j; }                               /* l.105068 */
        }
        if (best < 1.0000000000000002e-10) {                                    /* l.105078 */
            out->s[i]->e[0].b13 &= ~2; out->s[jb]->e[0].b13 &= ~2;             /* l.105079-105082 */
            Sh *a = out->s[i], *b = out->s[jb];
            for (int k = 0; k < b->n; k++) sh_push(a, b->e[k]);
            sh_upd_bbox(a);
            sl_erase(out, jb);                                                  /* l.105103-105121 */
        }
    }
}
/* FUN_1800c1530 (f.104680-104836, DE 3.3): one closed there-and-back tour (dir 1) per root of the medial forest */
static ShL med_chains(const MaeL *M, double tolA, double tolB, double z) {
    ShL out = { 0 };
    for (int i = 0; i < M->n; i++) {
        if (M->r[i].parent != -1) continue;                                     /* l.104719 */
        Sh *sh = sh_new();
        chain_walk(i, M, tolA, tolB, sh, 0);
        if (!sh->n) { sh_free(sh); continue; }                                  /* l.104768-104781 */
        sh->z = z; sh_upd_bbox(sh); sh->dir = 1;                                /* l.104783-104792 (z: +0x60 of the last S shape = the region level) */
        sl_push(&out, sh);
    }
    chains_merge(&out);                                                         /* l.104801 */
    for (int i = 0; i < out.n; ) { if (!out.s[i]->n) sl_erase(&out, i); else i++; }   /* l.104802-104834 */
    return out;
}
/* FUN_1800c1220 (f.104580-104679; control flow dis 0x1800c1220-0x1800c148e, DE 3.6): length and entity count of the walk
   from entity k until every remaining entity belongs to a medial edge already met */
static void chain_cover_len(const Sh *sh, int k, double *L, int *cnt) {
    int n = sh->n; char *vis = calloc(n ? n : 1, 1);
    *L = 0; *cnt = 0;
    int u = k; long guard = 0;
    for (;;) {
        int s = u;
        do {
            if ((sh->e[s].b13 ^ sh->e[u].b13) & 1) break;                       /* direction bit changed */
            *L += e_len(&sh->e[u]); vis[u] = 1; u = (u + 1) % n;
        } while (sh->e[u].ref_ent == sh->e[s].ref_ent);
        for (int j = 0; j < n; j++) if (sh->e[j].ref_ent == sh->e[s].ref_ent) vis[j] = 1;   /* dis 0x1800c13c9-0x1800c13f5 */
        if (u == k) { *cnt = n; break; }                                        /* dis 0x1800c13f9 */
        int w = u, done = 0;
        while (vis[w]) { w = (w + 1) % n; if (w == k) { *cnt = u > k ? u - k : u - k + n; done = 1; break; } }   /* dis 0x1800c1400-0x1800c1426 */
        if (done) break;
        if (++guard > (long)n * n + 16) { *cnt = n; break; }                    /* ours: runaway guard */
    }
    free(vis);
}
/* FUN_1800c2d30 (f.105498-105583, DE 3.7): b13 bit0 = entity of a medial edge the path already traversed in an earlier run */
static void chain_retrace_bits(Sh *sh) {
    int n = sh->n; char *m = calloc(n ? n : 1, 1);
    int i = 0;
    while (i < n) {
        int j = i;
        while (j < n && sh->e[j].ref_ent == sh->e[i].ref_ent && ((sh->e[j].b13 ^ sh->e[i].b13) & 1) == 0) { if (m[j] == 0) m[j] = 1; j++; }
        for (int l = 0; l < n; l++) if (sh->e[l].ref_ent == sh->e[i].ref_ent && m[l] == 0) m[l] = 2;
        i = j;
    }
    for (int l = 0; l < n; l++) { if (m[l] == 2) sh->e[l].b13 |= 1; else sh->e[l].b13 &= ~1; }
    free(m);
}
/* FUN_1800c1a40 (f.104839-104906, DE 3.6): each tour -> the shortest open walk from a candidate start (b13 bit1) that covers
   every medial edge; rotate (FUN_18014fd50, only k > 0 on a closed shape), cut to its count, open (dir 0), re-traversal bits */
static void chains_trim(ShL *out) {
    for (int q = 0; q < out->n; q++) {
        Sh *sh = out->s[q];
        double best = 3.4028234663852886e38; int kb = -1, cb = 0;
        for (int k = 0; k < sh->n; k++) {
            if (!(sh->e[k].b13 & 2)) continue;
            double L; int cnt; chain_cover_len(sh, k, &L, &cnt);
            if (L < best) { best = L; kb = k; cb = cnt; }                       /* strict (l.104876-104879) */
        }
        if (kb > 0 && sh->dir != 0) {                                           /* FUN_18014fd50 (f.180573-180719) */
            Ent *t = malloc(sh->n * sizeof *t);
            for (int k = 0; k < sh->n; k++) t[k] = sh->e[(kb + k) % sh->n];
            memcpy(sh->e, t, sh->n * sizeof *t); free(t);
        }
        if (cb < sh->n) sh->n = cb;                                             /* l.104891-104898 (kb = -1 -> empty) */
        sh->dir = 0;                                                            /* l.104899 */
        chain_retrace_bits(sh);                                                 /* l.104900 */
        sh_upd_bbox(sh);
    }
}
/* FUN_180110840 (f.146986-147109, ENG 6.7.1): keep only the nest group (islands..., outer) with the largest
   area(outer) - sum area(islands) (FUN_18010f210; strict >, start -1.0); area FUN_180126ac0 = the polygon area here (the
   offset shapes are line polygons) */
static double sh_area_abs(const Sh *s) { CPath c = sh_to_cpath(s, g_chord); double a = fabs(cl_area(&c)); free(c.xy); return a; }
static void sl_keep_largest_group(ShL *l) {
    double best = -1.0; int bs = -1, be = -1, i = 0;
    while (i < l->n) {
        int e = sl_group_end(l, i); if (e < 0) break;
        double v = sh_area_abs(l->s[e - 1]);
        for (int k = i; k < e - 1; k++) v -= sh_area_abs(l->s[k]);
        if (best < v) { best = v; bs = i; be = e; }
        i = e;
    }
    if (bs < 0) { sl_clear(l); return; }                                        /* (no group: nothing kept) */
    for (int k = l->n - 1; k >= be; k--) sl_erase(l, k);
    for (int k = bs - 1; k >= 0; k--) sl_erase(l, k);
}
/* FUN_180123a30 (f.156743-156807): keep the longest shape (strict >, start -1.0, first wins) */
static void sl_keep_longest(ShL *l) {
    double best = -1.0; int bi = -1;
    for (int i = 0; i < l->n; i++) { double L = sh_len(l->s[i]); if (best < L) { best = L; bi = i; } }
    if (bi < 0) return;
    for (int k = l->n - 1; k > bi; k--) sl_erase(l, k);
    for (int k = bi - 1; k >= 0; k--) sl_erase(l, k);
}
/* region -> CalcMedAx (S, M); 0 when CalcMedAx gives up (no convex corner / Cancel: Mastercam throws there, the entry then has
   no medial path - ours: treated as an empty medial axis) */
static int med_axis(const ShL *B, double arcTol, MxSL *S, MaeL *M) {
    MxSL R = { 0 };
    medax_from_shapes(B, &R);
    int ok = calc_med_ax(&R, S, M, arcTol, 8.0);
    msl_free(&R);
    if (!ok) M->n = 0;
    return ok && M->n > 0;
}

/* ============================================================================================ medial path (DE 4.1) */
/* FUN_180079260 (f.68418-68516): open medial path(s) of the tool-centre region inset by d (0x400: P+0x98 + 0.002, else
   0.002); arc tolerance 0.001, prune, tours, trim */
static ShL medial_path(Pock *C) {
    const McceParm *P = &C->P; ShL out = { 0 };
    double d = (P->flags & 0x400) ? P->troch_r + 0.002 : 0.002;                /* l.68464-68470 */
    ShL B = sl_offset(&C->cur, -d, C->z);                                       /* MCCECalcOffset(1, -d, C+0x340, 0x100) l.68471-68473 */
    if (B.n) {
        MxSL S = { 0 }; MaeL M = { 0 };
        if (med_axis(&B, 0.001, &S, &M)) {                                      /* l.68475-68477 */
            mae_prune(&S, &M);                                                  /* l.68478 */
            out = med_chains(&M, 0.001, 0.0001, C->z);                          /* l.68479-68480 */
            chains_trim(&out);                                                  /* l.68481 */
        }
        msl_free(&S); free(M.r);
    }
    sl_free(&B);
    return out;
}
/* start point of Mp[0].ent[0] (l.67925-67939: 'A' centre + r (cos sa, sin sa), otherwise +0x18); 0 when Mp has no first
   entity (Mastercam reads past the end there, DE 4 - ours: the caller takes the max-clearance point instead) */
static int medial_start(const ShL *Mp, P2 *c) {
    if (!Mp->n || !Mp->s[0]->n) return 0;
    const Ent *e = &Mp->s[0]->e[0];
    *c = e->id == 'A' ? p2(cos(e->sa) * e->r + e->c.x, sin(e->sa) * e->r + e->c.y) : e->p1;
    return 1;
}

/* FUN_180079570 (f.68523-68758, DE 4.2): arcs from the end of the helix H to the start of the medial path, arriving
   along its start tangent */
static void join_helix_medial(const Pock *C, const Sh *Mp0, Sh *H) {
    int climb = C->P.flags & 1;
    if (!H->n || H->e[H->n - 1].id != 'A') return;                              /* l.68557-68561 */
    Ent last = H->e[H->n - 1];
    double thE = last.sa + last.sw;                                             /* l.68562 */
    P2 t = p2(0, 0);                                                            /* FUN_1801204a0(Mp0, 0, 0.0, &t, 1e-6): unit start tangent */
    { P2 v = e_tan(&Mp0->e[0], 0.0); double l = plen(v); if (l >= 1e-12) t = pmul(v, 1.0 / l); }
    P2 v = climb ? p2(-t.y, t.x) : p2(t.y, -t.x);                               /* dis 0x180079626-0x180079683 (DE 4.2 correction) */
    if (fabs(v.x) > 1e-12 || fabs(v.y) > 1e-12) {                               /* dis 0x180079690-0x1800796a7 */
        double phi = atan2(v.y, v.x);
        double D = wrap1(phi - thE);                                            /* l.68585-68593 */
        if (climb && D < 0) D += 6.283185307179586;                             /* l.68594-68602 */
        if (!climb && D > 0) D -= 6.283185307179586;
        Ent a1 = ent_arc(last.c, last.r, thE, D);                               /* l.68603-68626 */
        P2 e1 = e_end(&a1);
        sh_push(H, a1);
        Ent a2 = ent_arc(p2((e1.x + last.c.x) * 0.5, (e1.y + last.c.y) * 0.5), last.r * 0.5, phi, climb ? 3.141592653589793 : -3.141592653589793);   /* l.68627-68679 */
        sh_push(H, a2);
    } else {
        /* l.68682-68753: ONE record with type 'A' (dis 0x180079728) holding line data: +0x18 = end of the last helix arc,
           +0x28/+0x30 = start of Mp0.ent[0], sweep 0 (zero-filled). Reproduced as is: a zero-sweep arc about the helix end
           with radius = start.x and start angle = start.y. (Unreachable in practice: the medial segments have length) */
        P2 s; medial_start(&(ShL){ (Sh **)&Mp0, 1, 1 }, &s);
        Ent a = ent_arc(e_end(&last), s.x, s.y, 0.0);
        sh_push(H, a);
    }
    sh_upd_bbox(H);
}

/* ============================================================================================ 0x200 (DE 4) */
/* FUN_180078130 (f.67870-67982): helix at the start of the open medial path, the join arcs, then the whole path */
static int entry_full_medial(Pock *C, Sh *E) {
    ShL Mp = medial_path(C);                                                    /* l.67921 */
    P2 c = p2(0, 0); double rho;
    if (!medial_start(&Mp, &c)) max_clearance(C, &C->cur, &c, &rho);            /* l.67922-67939 (FUN_180079050) */
    ShL tmp = { 0 };                                                            /* local_70: the helix loops are discarded (l.67941) */
    int have = helix_entry(C, c, E, &tmp);
    sl_free(&tmp);
    if (getenv("MCCE_MEDAX_DUMP")) fprintf(stderr, "0x200: medial path %d shapes, first %d ents len %.4f, centre (%.4f,%.4f), helix %d\n", Mp.n, Mp.n ? Mp.s[0]->n : 0, Mp.n ? sh_len(Mp.s[0]) : 0.0, c.x, c.y, have);
    if (have && E->n && Mp.n && Mp.s[0]->n) {                                   /* l.67943-67957 (X: retraced copy, air dwell only: not built) */
        join_helix_medial(C, Mp.s[0], E);
        for (int k = 0; k < Mp.s[0]->n; k++) sh_push(E, Mp.s[0]->e[k]);         /* FUN_180146b10 (f.175608-175635): entities as they are */
        sh_upd_bbox(E);
    }
    sl_free(&Mp);
    return have && E->n;
}

/* ============================================================================================ 0x1000 (DE 6) */
/* FUN_18007ca70 (f.70142-70166): the Profile entry's loop region: C+0x340 inset by max(0, w - 0.75 (R - r)) (mode 1) */
static ShL profile_loop_region(Pock *C, double w) {
    const McceParm *P = &C->P;
    return sl_offset(&C->cur, -fmax(0.0, w - 0.75 * (P->R - P->r)), C->z);
}
/* FUN_18007b6e0 (f.69537-69715): the ramp path = the longer of (a) the unpruned tours clipped by the Profile loop region
   (FUN_18007c4e0) and (b) the longest pruned tour; tie -> (b).
   (a) needs FUN_18015b2e0 (pairwise boolean primitive, ENG 6.5.6 / 6.7.3), whose set operation is NOT DECODED. Only its
   decoded early-outs are used here: (a) is known to be EMPTY when the loop region is empty (FUN_18007ca70 false), the tours
   are empty, or the bounding boxes of the last shapes of the two lists do not overlap (slack 1.1920928955078125e-07,
   l.187055-187061). Otherwise (a) is unknown:
     - (b) exists: (b) is used - ours: Mastercam's result only when (a) is not longer (*undecided = 1)
     - (b) is empty: no path can be decided -> *undecided = 2 (the caller refuses the method, NOT DECODED) */
static ShL medial_ramp_path(Pock *C, int *undecided) {
    const McceParm *P = &C->P; ShL out = { 0 };
    *undecided = 0;
    double d = 0.5 * P->slot_add + 0.002;                                       /* l.69611 */
    ShL B = sl_offset(&C->cur, -d, C->z);                                       /* l.69612-69614 */
    if (!B.n) return out;
    sl_keep_largest_group(&B);                                                  /* FUN_180110840 l.69616 */
    MxSL S = { 0 }; MaeL M = { 0 };
    med_axis(&B, 0.001, &S, &M);                                                /* l.69617-69619 */
    double s = 0, rho = -1.0;
    max_clear(&S, &M, &s, &rho);                                                /* FUN_1800c5df0 l.69620 */
    int xc_empty = 1;                                                           /* FUN_18007c4e0 (f.69989-70050) */
    {
        ShL Ch = med_chains(&M, 0.001, 0.0001, C->z);                           /* l.70022 (unpruned) */
        ShL L = profile_loop_region(C, rho + d);                                /* l.70023 */
        if (L.n && Ch.n) {
            sl_keep_largest_group(&L);                                          /* l.70025 */
            if (L.n) {
                const Sh *g = Ch.s[Ch.n - 1], *h = L.s[L.n - 1]; const double eps = 1.1920928955078125e-07;
                if (g->bb[0] - eps <= h->bb[2] && h->bb[0] <= g->bb[2] + eps && g->bb[1] - eps <= h->bb[3] && h->bb[1] <= g->bb[3] + eps)
                    xc_empty = 0;                                               /* FUN_18015b2e0 l.70026: NOT DECODED */
            }
        }
        sl_free(&Ch); sl_free(&L);
    }
    mae_prune(&S, &M);                                                          /* l.69635 */
    ShL Y = med_chains(&M, 0.001, 0.0001, C->z);                                /* l.69636-69637 */
    sl_keep_longest(&Y);                                                        /* l.69638 (no trim: a closed tour, dir 1) */
    double LY = Y.n ? sh_len(Y.s[0]) : -1.0;                                    /* l.69639-69643 */
    if (LY >= 0) { out = Y; Y.s = NULL; Y.n = Y.cap = 0; if (!xc_empty) *undecided = 1; }           /* l.69644-69656: LX <= LY -> Y */
    else if (!xc_empty) *undecided = 2;
    sl_free(&Y); msl_free(&S); free(M.r); sl_free(&B);
    return out;
}
/* FUN_18007b3c0 (f.69450-69536): ramp along the medial tour, widened by the additional slot width (P+0xa0) */
static int entry_medial_ramp(Pock *C, Sh *E, int *undecided) {
    const McceParm *P = &C->P;
    ShL Mp = medial_ramp_path(C, undecided);                                    /* l.69482 */
    if (!Mp.n) return 0;                                                        /* E empty: no Profile fallback for 0x1000 (0x8700) */
    ShL X = { 0 };
    if (1e-06 <= P->slot_add) {                                                 /* l.69484-69488 */
        /* MCCECalcOffset(1, 0.5 P+0xa0, Mp, X, mode 0): the offset of the degenerate there-and-back tour (mode bits NOT
           DECODED, ENG 6.5) - read as the region the disc of that radius sweeps along the tour (Clipper open-path round
           offset; [inferred]) */
        CPaths in; in.n = 0; in.p = calloc(Mp.n, sizeof(CPath));
        for (int i = 0; i < Mp.n; i++) {
            CPath c = { malloc((Mp.s[i]->n + 1) * 2 * sizeof(double)), 0 };
            for (int k = 0; k <= Mp.s[i]->n; k++) { P2 q = k < Mp.s[i]->n ? e_start(&Mp.s[i]->e[k]) : e_end(&Mp.s[i]->e[k - 1]); c.xy[2 * c.n] = q.x; c.xy[2 * c.n + 1] = q.y; c.n++; }
            in.p[in.n++] = c;
        }
        CPaths o = cl_offset_lines(&in, 0.5 * P->slot_add, 0, g_chord);
        X = cpaths_to_sl(&o, C->z); cl_free(&in); cl_free(&o);
        sl_keep_longest(&X);                                                    /* FUN_180123a30 */
    } else X = sl_copy(&Mp);                                                    /* AppendByCopy l.69491 */
    sl_free(&Mp);
    int ok = 0;
    if (X.n && X.s[0]->n) {
        /* FUN_18004ae80(this, this+0x340, X[0]) l.69498; the Clipper model (MCCE_MEASURE=clipper) keeps the unsmoothed loop */
        if (exact_on()) smooth_entry_loop(C, &C->cur, X.s[0]);
    }
    if (X.n && X.s[0]->n) {                                                     /* l.69499-69500 */
        int want = (P->flags & 1) ? 1 : -1;                                     /* l.69503-69508 */
        if (X.s[0]->dir != want) sh_rev(X.s[0]);
        ramp_loop(C, X.s[0], E);                                                /* FUN_18007bc00 l.69510 */
        ok = E->n > 0;
    }
    sl_free(&X);
    return ok;
}


/* ============================================================================================ 0x400 (DE 5, TW) */
/* record helpers (TW 0.2-0.5); rt = C+0xa0 = P+0x98 (trochoid loop radius) */
static int tw_nz(P2 v) { return fabs(v.x) > 1e-12 || fabs(v.y) > 1e-12; }
static P2 tw_n(int climb, P2 T) { return climb ? p2(T.y, -T.x) : p2(-T.y, T.x); }      /* n(T): right normal if climb, else left */
static P2 tw_perp(int climb, P2 W) { return climb ? p2(-W.y, W.x) : p2(W.y, -W.x); }   /* W rotated +90 (climb) / -90 */
static double tw_dirfix(int climb, double d) { if ((d < 0 && climb) || (d > 0 && !climb)) d += (d >= 0 ? -6.283185307179586 : 6.283185307179586); return d; }
static double tw_len(P2 v) { return sqrt(v.x * v.x + v.y * v.y); }
static double tw_pi(int climb) { return climb ? 3.141592653589793 : -3.141592653589793; }
/* TanS / TanE (unnormalised; e.g. l.70582-70611, l.71569-71606): 'L' e2 - e1; 'A' v = r (cos, sin)(angle) |sw|, sw > 0 ->
   (-v.y, v.x), sw < 0 -> (v.y, -v.x), sw == 0 -> (0, 0); else (0, 0) */
static P2 tw_tan(const Ent *e, int end) {
    if (e->id == 'L') return psub(e->p2, e->p1);
    if (e->id == 'A') {
        double a = end ? e->sa + e->sw : e->sa;
        P2 v = p2(cos(a) * e->r * fabs(e->sw), sin(a) * e->r * fabs(e->sw));
        if (e->sw > 0) return p2(-v.y, v.x);
        if (e->sw < 0) return p2(v.y, -v.x);
    }
    return p2(0, 0);
}
/* end point of the last entity of H ('L' e2, 'A' end, else +0x18) */
static P2 tw_hend(const Sh *H) { return H->n ? e_end(&H->e[H->n - 1]) : p2(0, 0); }
/* FUN_18011ffd0(H, n - 1, 1.0, &T, 1e-5): unit end tangent of the last entity (kernel, mcce_engines.md l.1997) */
static P2 tw_hend_tan(const Sh *H) { if (!H->n) return p2(0, 0); P2 t = e_tan(&H->e[H->n - 1], 1.0); double l = plen(t); return l >= 1e-12 ? pmul(t, 1.0 / l) : p2(0, 0); }
/* CalcCirCirIntersect @180007739 (f.13288-13398, exact): 0 / 1 / 2 points; the smaller circle is A */
static int cir_cir(P2 c1, double r1, P2 c2, double r2, P2 X[2]) {
    double dx = c1.x - c2.x, dy = c1.y - c2.y, d = sqrt(dy * dy + dx * dx);
    if (!(d <= fabs(r2 + r1) + 1e-07 && 1e-07 <= d)) return 0;
    P2 A = c1, B = c2; double rb = r2, rs = r1;
    if (r2 < r1) { A = c2; B = c1; rb = r1; rs = r2; }
    double ux = B.x - A.x, uy = B.y - A.y, a = ((d * d + rs * rs) - rb * rb) / (d * 2.0), l = sqrt(uy * uy + ux * ux);
    if (!(1e-12 <= l)) return 0;
    uy /= l; ux /= l;
    P2 F = p2(ux * a + A.x, uy * a + A.y);
    double h2 = rs * rs - a * a;
    if (!(-1e-07 <= h2)) return 0;
    if (1e-13 <= h2) { double h = sqrt(h2); X[0] = p2(-uy * h + F.x, ux * h + F.y); X[1] = p2(-uy * -h + F.x, ux * -h + F.y); return 2; }
    X[0] = F; return 1;                                                         /* (FUN_1800a5150 after it: no effect) */
}
/* FUN_1800a9000 (f.92047-92175): line {P, D} vs circle (c, r); parameters in units of D, ordered along +D; 0 / 1 / 2.
   (The CalcLnLnIntersectPt failure branch FUN_1800a9490 is not reached with a non-zero D: not ported, returns 0) */
static int ln_cir(P2 c, double r, P2 P, P2 D, double t[2]) {
    t[0] = t[1] = 0.0;
    double tf; P2 F;
    if (!lnln(P, D, c, p2(-D.y, D.x), &tf, NULL, &F)) return 0;
    double rm = r - 1e-14, d2 = (c.y - F.y) * (c.y - F.y) + (c.x - F.x) * (c.x - F.x);
    if (rm < 0.0) rm = 0.0;
    if (!(d2 <= rm * rm || (r + 1e-06) * (r + 1e-06) <= d2)) { t[0] = tf; return 1; }
    if (!(d2 <= r * r)) return 0;
    double h = sqrt(r * r - d2), Ld = sqrt(D.y * D.y + D.x * D.x);
    P2 o = p2(D.x * (1.0 / Ld) * h, D.y * (1.0 / Ld) * h);
    P2 X[2] = { p2(F.x - o.x, F.y - o.y), p2(o.x + F.x, o.y + F.y) };
    if (fabs(D.x) <= fabs(D.y)) { t[0] = (X[0].y - P.y) / D.y; t[1] = (X[1].y - P.y) / D.y; }
    else { t[0] = (X[0].x - P.x) / D.x; t[1] = (X[1].x - P.x) / D.x; }
    int cnt = 2;
    for (int i = 0; i < cnt; i++) {
        double dd = sqrt((c.y - X[i].y) * (c.y - X[i].y) + (c.x - X[i].x) * (c.x - X[i].x));
        if (1e-06 < fabs(dd - r)) { if (i == 0 && cnt == 2) { X[0] = X[1]; t[0] = t[1]; } cnt--; i--; }
    }
    if (cnt == 2 && fabs(t[0] - t[1]) < 1e-12) cnt = 1;
    return cnt;
}

/* ---------------------------------------------------------------------------------------------- step length (TW 3) */
/* FUN_180081d40 (f.72661-72808): one model loop in its own frame (previous centre at the origin, advance x along +y) */
typedef struct { double d, rt, x; int flags; Ent rec[3]; double len[3], tot; } TwLoop;
static void tw_model(double d, double rt, int flags, double x, TwLoop *m) {
    m->d = d; m->rt = rt; m->x = x; m->flags = flags;
    m->rec[0] = ent_line(p2(rt, 0), p2(rt, x));
    m->rec[1] = ent_arc(p2(0, x), rt, 0.0, 3.141592653589793);
    m->rec[2] = ent_line(p2(-rt, x), p2(-rt, 0));
    if (!(flags & 1)) {                                                         /* l.72710-72760 */
        Ent t = m->rec[0]; m->rec[0] = m->rec[2]; m->rec[2] = t;
        for (int k = 0; k < 3; k++) m->rec[k] = e_rev(&m->rec[k]);
    }
    for (int k = 0; k < 3; k++) m->len[k] = e_len(&m->rec[k]);
    m->tot = m->len[1] + m->len[0] + m->len[2];                                 /* l.72804 */
}
/* FUN_1800827c0 (f.72968-73164): engagement angle at u in [0, 1] of the model loop */
static double tw_eng(const TwLoop *m, double u) {
    int climb = m->flags & 1, k; double s = m->tot * u, t;
    if (m->len[0] <= s) { if (m->len[1] + m->len[0] <= s) { k = 2; t = ((s - m->len[0]) - m->len[1]) / m->len[2]; } else { k = 1; t = (s - m->len[0]) / m->len[1]; } }
    else { k = 0; t = s / m->len[0]; }
    const Ent *e = &m->rec[k];
    P2 P = e_at(e, t), T;
    if (e->id == 'L') T = psub(e->p2, e->p1);
    else { double a = e->sw * t + e->sa; Ent q = *e; q.sa = a; q.sw = e->sw; T = tw_tan(&q, 0); }
    P2 mm = tw_n(climb, T); double ml = sqrt(mm.y * mm.y + mm.x * mm.x);
    if (1e-12 <= ml) mm = p2(mm.x / ml, mm.y / ml);
    P2 X[2]; double r1 = m->rt + m->d;
    int cnt = cir_cir(p2(0, 0), r1, P, m->d, X);
    if (cnt == 0) return ((0.0 - P.y) * (0.0 - P.y) + (0.0 - P.x) * (0.0 - P.x) <= r1 * r1) ? 0.0 : 3.141592653589793;
    double th[2] = { 0, 0 };
    for (int i = 0; i < cnt; i++) {
        P2 v = psub(X[i], P); double l = sqrt(v.y * v.y + v.x * v.x);
        if (1e-12 <= l) v = p2(v.x / l, v.y / l);
        double a = acos(v.y * mm.y + v.x * mm.x);                              /* no clamp */
        if (v.y * mm.x - mm.y * v.x < 0.0) a = -a;
        if ((a < 0 && climb) || (a > 0 && !climb)) a += (0.0 < a || a == 0.0) ? -6.283185307179586 : 6.283185307179586;
        th[i] = a;
    }
    return (cnt != 2 || fabs(th[0]) <= fabs(th[1])) ? fabs(th[0]) : fabs(th[1]);
}
/* FUN_1800aed10 (f.94821-95021) via FUN_1800aeca0 / FUN_1800aec80: Numerical-Recipes brent minimiser of -g on the bracket
   (0, 0.5, 1); CGOLD 0.381966, ZEPS = min(tol, 1e-10), 100 iterations (TW 3.4: NR line by line [inferred]) */
static double tw_eng_max(const TwLoop *m, double tol) {
    double ax = 0.0, bx = 0.5, cx = 1.0, ZEPS = tol >= 1e-10 ? 1e-10 : tol;
    double a = ax < cx ? ax : cx, b = ax <= cx ? cx : ax, x = bx, w = bx, v = bx, d = 0.0, e = 0.0;
    double fx = -tw_eng(m, x), fw = fx, fv = fx;
    for (int it = 0; it < 100; it++) {
        double xm = (a + b) * 0.5, tol1 = fabs(x) * tol + ZEPS, tol2 = tol1 * 2.0;
        if (fabs(x - xm) <= tol2 - (b - a) * 0.5) break;
        if (fabs(e) > tol1) {
            double r = (x - w) * (fx - fv), q = (x - v) * (fx - fw), p = (x - v) * q - (x - w) * r;
            q = (q - r) * 2.0; if (q > 0.0) p = -p; q = fabs(q);
            double etemp = e; e = d;
            if (fabs(p) >= fabs(0.5 * q * etemp) || p <= q * (a - x) || p >= q * (b - x)) { e = (x >= xm ? a - x : b - x); d = 0.381966 * e; }
            else { d = p / q; double u = x + d; if (u - a < tol2 || b - u < tol2) d = xm - x >= 0 ? fabs(tol1) : -fabs(tol1); }
        } else { e = (x >= xm ? a - x : b - x); d = 0.381966 * e; }
        double u = fabs(d) >= tol1 ? x + d : x + (d >= 0 ? fabs(tol1) : -fabs(tol1));
        double fu = -tw_eng(m, u);
        if (fu <= fx) { if (u >= x) a = x; else b = x; v = w; w = x; x = u; fv = fw; fw = fx; fx = fu; }
        else {
            if (u < x) a = u; else b = u;
            if (fu <= fw || w == x) { v = w; w = u; fv = fw; fw = fu; }
            else if (fu <= fv || v == x || v == w) { v = u; fv = fu; }
        }
    }
    return -fx;
}
/* FUN_1800826d0 (f.72939-72967, the callback LAB_180006aaa): target angle minus the loop's largest engagement */
typedef struct { int flags; double d, a, rt; } TwStepCtx;
static double tw_step_fn(void *vc, double x) {
    const TwStepCtx *c = vc; TwLoop m; tw_model(c->d, c->rt, c->flags, x, &m);
    return c->a - tw_eng_max(&m, 1e-7);
}
/* FUN_180083a40 (f.73482-73524): the loop advance */
static double tw_step(const Pock *C) {
    const McceParm *P = &C->P;
    TwStepCtx c = { (int)P->flags, P->R - P->r, 0, P->troch_r };
    c.a = ln_eng_ang(c.d, P->s_work);                                           /* CalcLnEngAng(d, C+0x200) l.73503-73504 */
    double D = 0; brent(tw_step_fn, &c, 1e-4, c.d * 2.0, 1e-7, &D);            /* return value ignored */
    if (P->r > 1e-7) {                                                          /* l.73510-73519 */
        c.d = P->R; c.a = P->eng;
        double D2 = 0; brent(tw_step_fn, &c, 1e-4, c.d * 2.0, 1e-7, &D2);
        if (D2 < D) D = D2;
    }
    return D;
}

/* ---------------------------------------------------------------------------------------------- leaf builders (TW 4) */
typedef struct { Ent prev, cur; } TwSeg;                                       /* seg (local_b8): prev rec, cur rec */
/* FUN_180081190 (f.72313-72557): the trochoid loop on seg.cur */
static void tw_loop(const Pock *C, const TwSeg *g, Sh *H) {
    int climb = C->P.flags & 1; double rt = C->P.troch_r;
    P2 S1 = e_start(&g->cur), E1 = e_end(&g->cur), m = tw_n(climb, tw_tan(&g->cur, 0));
    double ml = tw_len(m); if (ml < 1e-12) return;
    P2 N = pmul(m, rt / ml);
    if (!(fabs(N.x) > 1e-6 || fabs(N.y) > 1e-6)) return;
    double a = atan2(N.y, N.x);
    sh_push(H, ent_line(padd(S1, N), padd(E1, N)));
    sh_push(H, ent_arc(E1, rt, a, tw_pi(climb)));
    sh_push(H, ent_line(psub(E1, N), psub(S1, N)));
}
/* FUN_18007f0d0 (f.71255-71471): first step */
static int tw_first(const Pock *C, const TwSeg *g, Sh *H) {
    int climb = C->P.flags & 1; double rt = C->P.troch_r;
    P2 S1 = e_start(&g->cur), m = tw_n(climb, tw_tan(&g->cur, 0));
    if (!tw_nz(m)) return 0;
    double a1 = atan2(m.y, m.x);
    P2 V = psub(tw_hend(H), S1);
    if (!tw_nz(V)) return 1;
    double a0 = atan2(V.y, V.x), sw = tw_dirfix(climb, wrap1(a1 - a0));
    if (fabs(sw * rt) > 1e-12) sh_push(H, ent_arc(S1, rt, a0, sw));
    tw_loop(C, g, H);
    return 1;
}
/* FUN_18007d5a0 (f.70471-70904): fallback transition (back = 0 from e360, 1 from e3d0) */
static int tw_fallback(const Pock *C, int back, const TwSeg *g, Sh *H) {
    int climb = C->P.flags & 1; double rt = C->P.troch_r;
    P2 S0 = e_start(&g->prev), E0 = e_end(&g->prev), t0 = tw_tan(&g->prev, 0); double l0 = tw_len(t0);
    if (l0 < 1e-12) return 0;
    t0 = pmul(t0, 1.0 / l0);
    P2 S1 = e_start(&g->cur), t1 = tw_tan(&g->cur, 0); double l1 = tw_len(t1);
    if (l1 < 1e-12) return 0;
    t1 = pmul(t1, 1.0 / l1);
    P2 N0 = tw_n(climb, t0), N1 = tw_n(climb, t1);
    if (!tw_nz(N0)) { sh_push(H, ent_arc(S0, rt, 0.0, 0.0)); return 0; }       /* l.70770-70775 (unit: not reached) */
    sh_push(H, ent_arc(S0, rt, atan2(-N0.y, -N0.x), tw_pi(climb)));
    sh_push(H, ent_line(padd(S0, pmul(N0, rt)), padd(E0, pmul(N0, rt))));
    double a0 = atan2(N0.y, N0.x);
    P2 V = back ? pmul(N1, -1.0) : N1;
    if (!tw_nz(V)) return 0;
    double a1 = atan2(V.y, V.x), sw = tw_dirfix(climb, wrap1(a1 - a0));
    if (fabs(sw * rt) > 1e-12) sh_push(H, ent_arc(S1, rt, a0, sw));
    if (back) sh_push(H, ent_arc(psub(S1, pmul(N1, 0.5 * rt)), 0.5 * rt, a1, tw_pi(climb)));
    return 1;
}
/* FUN_18007f8c0 (f.71519-71941): new -> new turn joint */
static int tw_turn(const Pock *C, const TwSeg *g, Sh *H) {
    int climb = C->P.flags & 1; double rt = C->P.troch_r;
    P2 S0 = e_start(&g->prev), T0 = tw_tan(&g->prev, 1);
    if (!tw_nz(T0)) return 0;
    double al0 = atan2(T0.y, T0.x);
    P2 S1 = e_start(&g->cur), T1 = tw_tan(&g->cur, 0);
    if (!tw_nz(T1)) return 0;
    double turn = wrap1(atan2(T1.y, T1.x) - al0);
    if ((turn > 0 && climb) || (turn < 0 && !climb)) return 0;
    P2 m0 = tw_n(climb, T0); double l0 = tw_len(m0); if (l0 < 1e-12) return 0;
    P2 N0 = pmul(m0, rt / l0), P1 = psub(S0, N0);
    P2 m1 = tw_n(climb, T1); double l1 = tw_len(m1); if (l1 < 1e-12) return 0;
    P2 N1 = pmul(m1, rt / l1), Q1 = padd(S1, N1);
    double s1 = 0, s2 = 0; P2 X = p2(0, 0);
    int ok = lnln(P1, T0, Q1, T1, &s1, &s2, &X);
    if (!(ok && s1 <= 0 && s2 <= 0)) return 0;
    if (pdist2(P1, X) < 1.0000000000000002e-10) return 0;
    double phi = turn * 0.5 + al0, s3 = 0, s4 = 0; P2 Y = p2(0, 0);
    ok = lnln(X, p2(cos(phi), sin(phi)), P1, N0, &s3, &s4, &Y);
    double rho = pdist(Y, P1);
    if (!(ok && s3 >= 0 && 0 <= s4 && s4 <= 1 && rho >= rt * 0.5 && tw_nz(pmul(N0, -1.0)))) return 0;
    Ent a = ent_arc(Y, rho, atan2(-N0.y, -N0.x), (3.141592653589793 - fabs(turn)) * (climb ? 1.0 : -1.0));
    sh_push(H, a);
    sh_push(H, ent_line(e_end(&a), Q1));
    return 1;
}
/* FUN_1800805d0 (f.71942-72312): new -> retrace joint back onto the path */
static int tw_back(const Pock *C, const TwSeg *g, Sh *H) {
    int climb = C->P.flags & 1; double rt = C->P.troch_r;
    P2 S0 = e_start(&g->prev), T0 = tw_tan(&g->prev, 1);
    if (!tw_nz(T0)) return 0;
    double al0 = atan2(T0.y, T0.x);
    P2 S1 = e_start(&g->cur), T1 = tw_tan(&g->cur, 0);
    if (!tw_nz(T1)) return 0;
    double turn = wrap1(atan2(T1.y, T1.x) - al0);
    P2 m0 = tw_n(climb, T0); double l0 = tw_len(m0); if (l0 < 1e-12) return 0;
    P2 N0 = pmul(m0, rt / l0), P1 = psub(S0, N0), X = p2(0, 0), Y;
    double s1, s2;
    if (!lnln(P1, T0, S1, T1, &s1, &s2, &X)) Y = S0;                           /* dis 0x180080b47 */
    else {
        double phi = turn * 0.5 + al0, s3 = 0, s4 = 0; Y = p2(0, 0);
        int ok2 = lnln(X, p2(cos(phi), sin(phi)), P1, N0, &s3, &s4, &Y);
        if (!ok2 || s4 < 0 || s4 > 1) return 0;
    }
    double rho = pdist(Y, P1);
    if (!(rho >= rt * 0.5 && tw_nz(pmul(N0, -1.0)))) return 0;
    Ent a = ent_arc(Y, rho, atan2(-N0.y, -N0.x), (3.141592653589793 - fabs(turn)) * (climb ? 1.0 : -1.0));
    sh_push(H, a);
    sh_push(H, ent_line(e_end(&a), S1));
    return 1;
}
/* FUN_18007e540 (f.70963-71218): retrace -> new */
static void tw_renew(const Pock *C, const TwSeg *g, Sh *H) {
    int climb = C->P.flags & 1; double rt = C->P.troch_r;
    P2 E0 = e_end(&g->prev), m = tw_n(climb, tw_tan(&g->prev, 1)); double ml = tw_len(m);
    if (ml < 1e-12) return;
    P2 D = pmul(m, rt * 0.5 / ml);
    if (fabs(D.x) <= 1e-12 && fabs(D.y) <= 1e-12) return;
    double a = atan2(D.y, D.x);
    P2 Cc = psub(E0, D), m1 = tw_n(climb, tw_tan(&g->cur, 0));
    if (!tw_nz(m1)) return;
    double b = atan2(m1.y, m1.x), a2 = a + 3.141592653589793;
    while (a2 > 3.141592653589793) a2 -= 6.283185307179586;
    while (a2 < -3.141592653589793) a2 += 6.283185307179586;
    double sw = tw_dirfix(climb, wrap1(b - a2));
    sh_push(H, ent_arc(Cc, rt * 0.5, a, tw_pi(climb)));
    sh_push(H, ent_arc(E0, rt, a2, sw));
    tw_loop(C, g, H);
}

/* ---------------------------------------------------------------------------------------------- walker (TW 2) */
typedef struct { int sh, ent; double t; P2 pt; } TwPos;
typedef struct { TwPos prev, cur; int prevFlag, curFlag; } TwWalk;
/* FUN_1800832e0 (f.73297-73371): end of the current run of new entities, from W->prev */
static TwPos tw_run_end(const Sh *M0, const TwWalk *W) {
    TwPos o = W->prev; o.t = 1.0;
    if (M0->n) {
        int n = M0->n, i = W->prev.ent;
        if (W->prev.ent != W->cur.ent) do { if (M0->e[i].b13 & 1) break; o.ent = i; i = (i + 1) % n; } while (i != W->cur.ent);
        o.pt = e_end(&M0->e[o.ent]);
    }
    return o;
}
/* FUN_180083f30 (f.73606-73724) */
static int tw_walk(const Sh *M0, double delta, TwSeg *g, TwWalk *W) {
    g->prev = g->cur; W->prev = W->cur; W->prevFlag = W->curFlag;               /* l.73640-73651 */
    ShL L = { (Sh **)&M0, 1, 1 }; Pos from = { 0, W->prev.ent, W->prev.t, W->prev.pt }, to; double walked = 0;
    int ok = walk_fwd(0, delta, &L, &from, &to, &walked);                       /* FUN_18006f1c0 mode 0 l.73652 */
    W->cur.ent = to.ent; W->cur.t = to.t; W->cur.pt = to.pt;
    if (!(ok || walked >= 1e-4)) { W->cur.ent = -1; return ok; }               /* l.73653, l.73718-73720 */
    W->curFlag = ~M0->e[W->cur.ent].b13 & 1;                                    /* l.73654-73656 */
    if (W->prevFlag && !W->curFlag) {                                           /* l.73657-73670 */
        TwPos q = tw_run_end(M0, W);
        double Ls = sec_len(M0, W->prev.ent, W->prev.t, q.ent, q.t);
        if (Ls > 1e-4) { W->cur.ent = q.ent; W->cur.t = q.t; W->cur.pt = q.pt; W->curFlag = 1; }
    }
    if (!W->curFlag) { W->cur.t = 1.0; W->cur.pt = e_end(&M0->e[W->cur.ent]); }   /* l.73672-73707 */
    g->cur = ent_line(W->prev.pt, W->cur.pt);                                   /* l.73709-73716 */
    return 1;
}

/* ---------------------------------------------------------------------------------------------- join (TW 6) */
/* FUN_180084210 (f.73725-73827): the helix end is already on the entry circle and tangent */
static int tw_join_tangent(const Pock *C, P2 S, const Sh *H) {
    int climb = C->P.flags & 1; double rt = C->P.troch_r;
    P2 E = tw_hend(H);
    if (!(pdist(S, E) - rt <= 1e-5)) return 0;
    P2 TE = tw_hend_tan(H); if (!tw_nz(TE)) return 0;
    double aT = atan2(TE.y, TE.x);
    P2 u = tw_perp(climb, psub(E, S)); if (!tw_nz(u)) return 0;
    return fabs(wrap1(atan2(u.y, u.x) - aT)) <= 0.08726646259971647;
}
/* FUN_1800834f0 (f.73372-73481): target point Q on the entry circle */
static int tw_join_target(const Pock *C, P2 S, const Sh *H, P2 *Q) {
    int climb = C->P.flags & 1; double rt = C->P.troch_r, beta;
    P2 E = tw_hend(H), TE = tw_hend_tan(H), V = psub(E, S);
    if (tw_nz(V)) beta = atan2(V.y, V.x); else if (tw_nz(TE)) beta = atan2(TE.y, TE.x); else return 0;
    double Ld = pdist(S, E), den = -2.0 * rt * Ld, m = ((rt + Ld) - fabs(rt - Ld)) * 0.5 + fabs(rt - Ld);
    if (fabs(den) < 1e-7) return 0;
    double g = acos((m * m - rt * rt - Ld * Ld) / den), ang = climb ? beta + g : beta - g;
    *Q = p2(S.x + rt * cos(ang), S.y + rt * sin(ang));
    return 1;
}
/* FUN_180082120 (f.72809-72938): cubic control points E -> Q */
static int tw_join_bez(const Pock *C, P2 S, const Sh *H, P2 Q, P2 bez[4]) {
    int climb = C->P.flags & 1;
    P2 E = tw_hend(H), TE = tw_hend_tan(H); double lt = tw_len(TE);
    if (lt < 1e-12) return 0;
    P2 u = tw_perp(climb, psub(Q, S)); double lu = tw_len(u);
    if (lu < 1e-12) return 0;
    double Lq = pdist(E, Q);
    bez[0] = E; bez[1] = padd(E, pmul(TE, (1.0 / lt) * Lq * 0.3333333333333333));
    bez[2] = psub(Q, pmul(u, (1.0 / lu) * Lq * 0.3333333333333333)); bez[3] = Q;
    return 1;
}
/* AppendBezier FUN_1800a5b00(1e-5, bez, H): the fit (FUN_1800aafd0 / FUN_1800aca30) is NOT DECODED - ours: chords of the
   cubic within 1e-5 (uniform in t, n from the second-difference bound) */
static void tw_append_bezier(const P2 b[4], Sh *H) {
    double m1 = tw_len(p2(b[0].x - 2 * b[1].x + b[2].x, b[0].y - 2 * b[1].y + b[2].y)), m2 = tw_len(p2(b[1].x - 2 * b[2].x + b[3].x, b[1].y - 2 * b[2].y + b[3].y));
    int n = (int)ceil(sqrt(0.75 * fmax(m1, m2) / 1e-5)); if (n < 1) n = 1; if (n > 100000) n = 100000;
    P2 prev = b[0];
    for (int i = 1; i <= n; i++) {
        double t = (double)i / n, s = 1 - t;
        P2 q = p2(s * s * s * b[0].x + 3 * s * s * t * b[1].x + 3 * s * t * t * b[2].x + t * t * t * b[3].x, s * s * s * b[0].y + 3 * s * s * t * b[1].y + 3 * s * t * t * b[2].y + t * t * t * b[3].y);
        sh_push(H, ent_line(prev, q)); prev = q;
    }
}
/* FUN_180083c10 (f.73525-73605): a new entity end leaves the entry disk */
static int tw_join_leaves(const Pock *C, P2 S, const Sh *H, int n0) {
    double lim = (C->P.troch_r <= C->P.helix_r ? C->P.helix_r : C->P.troch_r) + 1e-5;
    for (int i = n0; i < H->n; i++) if (pdist2(e_end(&H->e[i]), S) > lim * lim) return 1;
    return 0;
}
/* FUN_180082e80 (f.73165-73296): first hit of the helix-end ray with the entry circle */
static P2 tw_join_hit(const Pock *C, P2 S, const Sh *H) {
    double rt = C->P.troch_r, t[2];
    P2 E = tw_hend(H), TE = tw_hend_tan(H);
    int cnt = ln_cir(S, rt, E, TE, t);
    if (cnt == 1) return padd(E, pmul(TE, t[0]));
    if (cnt == 2) return padd(E, pmul(TE, t[0] < 0 ? t[1] : t[0]));
    if (cnt == 0) { P2 v = psub(E, S); double phi = tw_nz(v) ? atan2(v.y, v.x) : 0.0; return p2(S.x + rt * cos(phi), S.y + rt * sin(phi)); }
    return E;
}
/* FUN_18007f730 (f.71472-71518) */
static void tw_join(const Pock *C, const Sh *M0, Sh *H) {
    P2 S = e_start(&M0->e[0]);
    int n0 = H->n;
    if (tw_join_tangent(C, S, H)) return;
    P2 Q, bez[4];
    if (tw_join_target(C, S, H, &Q) && tw_join_bez(C, S, H, Q, bez)) {
        tw_append_bezier(bez, H);
        if (!tw_join_leaves(C, S, H, n0)) { sh_upd_bbox(H); return; }
        H->n = n0;                                                              /* FUN_180026fa0 l.71511 */
    }
    sh_push(H, ent_line(tw_hend(H), tw_join_hit(C, S, H)));                     /* FUN_18007d400 + FUN_18011d2d0 */
}

/* FUN_180081ab0 (f.72558-72658): trochoidal walk along the open medial path M0, appended to H */
static void troch_walk(const Pock *C, const Sh *M0, Sh *H) {
    tw_join(C, M0, H);                                                          /* l.72600 */
    double delta = tw_step(C);                                                  /* l.72601 */
    TwSeg g; memset(&g, 0, sizeof g);
    TwWalk W; memset(&W, 0, sizeof W); W.cur.pt = e_start(&M0->e[0]); W.curFlag = 1;   /* l.72594-72631 */
    long guard = 0;
    while (tw_walk(M0, delta, &g, &W)) {                                         /* l.72632-72654 */
        if (mcce_abort || ++guard > 2000000) break;                             /* ours: Cancel / runaway guard */
        if (g.prev.id == 0) tw_first(C, &g, H);
        else if (!W.prevFlag && !W.curFlag) sh_push(H, g.cur);                  /* FUN_18007ef90 */
        else if (!W.prevFlag && W.curFlag) tw_renew(C, &g, H);                  /* FUN_18007e540 */
        else if (W.prevFlag && !W.curFlag) { if (tw_back(C, &g, H) || tw_fallback(C, 1, &g, H)) sh_push(H, g.cur); }   /* FUN_18007e3d0 */
        else { if (tw_turn(C, &g, H) || tw_fallback(C, 0, &g, H)) tw_loop(C, &g, H); }                                 /* FUN_18007e360 */
    }
    sh_upd_bbox(H);                                                             /* l.72655-72656 */
}

/* FUN_180079cf0 (f.68763-68853): full circle of radius P+0x90 / 2 through the helix end and the medial start */
static void troch_mid_circle(const Pock *C, const Sh *M0, Sh *H) {
    P2 S = e_start(&M0->e[0]), E = tw_hend(H), Mid = p2((E.x + S.x) * 0.5, (E.y + S.y) * 0.5), v = psub(E, Mid);
    if (1e-12 < fabs(v.x) || 1e-12 < fabs(v.y)) { sh_push(H, ent_arc(Mid, C->P.helix_r * 0.5, atan2(v.y, v.x), (C->P.flags & 1) ? 6.283185307179586 : -6.283185307179586)); sh_upd_bbox(H); }
}
static Pock *dyn_pock_run(const McceParm *pin, ShL A, ShL B, ShL T3, const Ent *prev);
static void pock_free(Pock *C);
/* FUN_18007a150 (f.68860-69345): a recursive Dynamic Mill run that clears the disk at the medial start */
static void troch_start_disk(Pock *C, const Sh *M0, Sh *H) {
    const McceParm *P = &C->P;
    double e = P->helix_r - (P->R - P->r);                                      /* l.68997 */
    int small = fabs(e) <= 0.0001 && fabs(e) != 0.0001;
    if (small) troch_mid_circle(C, M0, H);                                      /* l.68998-69002 */
    P2 S = e_start(&M0->e[0]);
    double rho;
    if (P->helix_r < P->troch_r) rho = P->troch_r;
    else if (0.0 < e && !small) rho = P->helix_r;
    else return;                                                                /* l.69021-69023 */
    /* (the inner run has P+0x90 = P+0x98 = 0: e < 0 there, so it never comes back here) */
    ShL Mo = { 0 }, Ma = { 0 };
    Sh *m = sh_new(); sh_push(m, ent_arc(S, rho, 0.0, 6.283185307179586)); m->dir = 1; sh_upd_bbox(m); sl_push(&Mo, m);   /* l.69024-69071 */
    if (0.0 < e && !small) { Sh *i = sh_new(); sh_push(i, ent_arc(S, P->troch_r, 0.0, 6.283185307179586)); i->dir = 1; i->fl = 1; sh_upd_bbox(i); sl_push(&Ma, i); }   /* l.69075-69121 */
    { Sh *o = sh_new(); sh_push(o, ent_arc(S, P->troch_r, 0.0, 6.283185307179586)); o->dir = 1;
      if (Ma.n != 0) o->e[0].r += 0.0001;                                       /* l.69122-69174 */
      sh_upd_bbox(o); sl_push(&Ma, o); }
    McceParm Q = *P;                                                            /* CDynPockParm copy (the base fields) l.69175 */
    Q.flags = (P->flags & 0xeff0ffff) | 0x22000000;                             /* l.69176 */
    Q.first_off = 0; Q.troch_r = 0; Q.approach = 0; Q.helix_r = 0; Q.slot_add = 0; Q.plunge_ang = 0; Q.pitch = 0;   /* l.69177-69183 */
    Ent prev = H->n ? H->e[H->n - 1] : ent_line(S, S); prev.z = P->depth;      /* P'+0x118, +0x78 := C+0x88 l.69184-69204 */
    if (rho * 0.75 < Q.min_rad) { Q.min_rad = ((P->R - P->r) + rho) * 0.75 - (P->R - P->r); if (Q.min_rad < 0.0) Q.min_rad = 0.0; }   /* l.69206-69210 */
    double chord = g_chord;                                                     /* (ours: the port's chord global) */
    Pock *R2 = dyn_pock_run(&Q, Mo, Ma, (ShL){ 0 }, &prev);                     /* MCCECalcDynPock l.69212-69216 */
    g_chord = chord;
    if (R2->nd[0]) snprintf(C->nd, sizeof C->nd, "%s", R2->nd);
    int n = R2->T.n - 1;                                                        /* l.69228-69240 */
    while (n >= 0 && (R2->T.r[n].e.ce & 5)) n--;
    for (int k = 0; k <= n; k++) { Ent x = R2->T.r[k].e; x.ce = 0; x.b13 = 0; x.z = 0; sh_push(H, x); }   /* l.69241-69262 */
    if (R2->T.n) sh_upd_bbox(H);
    pock_free(R2); free(R2); sl_free(&Mo); sl_free(&Ma);
}
/* FUN_180078500 (f.67985-68083): helix at the start of the open medial path (inset by P+0x98 + 0.002), then the disk run and
   the trochoidal walk along the path */
static int entry_troch_medial(Pock *C, Sh *E) {
    ShL Mp = medial_path(C);                                                    /* l.68029 (d = P+0x98 + 0.002) */
    P2 c = p2(0, 0); double rho;
    if (!medial_start(&Mp, &c)) max_clearance(C, &C->cur, &c, &rho);            /* l.68030-68052 */
    ShL tmp = { 0 };                                                            /* local_60 discarded (l.68054) */
    int have = helix_entry(C, c, E, &tmp);
    sl_free(&tmp);
    if (getenv("MCCE_MEDAX_DUMP")) fprintf(stderr, "0x400: medial path %d shapes, first %d ents len %.4f, centre (%.4f,%.4f), helix %d\n", Mp.n, Mp.n ? Mp.s[0]->n : 0, Mp.n ? sh_len(Mp.s[0]) : 0.0, c.x, c.y, have);
    if (have && E->n && Mp.n && Mp.s[0]->n) {                                   /* l.68055-68061 */
        troch_start_disk(C, Mp.s[0], E);
        if (!C->nd[0]) troch_walk(C, Mp.s[0], E);
    }
    sl_free(&Mp);
    return have && E->n && !C->nd[0];
}
