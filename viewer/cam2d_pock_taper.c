/* Tapered walls of Pocket types 0 / 1 / 2 and of an Open pocket closed by the line (decision N9-19; included by cam2d_pock.c).
 *
 * FUN_180056a70 l.59008-59017 turns tapered walls on (see pko_taper_on); FUN_1801c5250 l.313636-313641 then processes the
 * chain pocket depth by depth, and at each depth process_pocket l.323256-323264 offsets the chains twice:
 *   1. FUN_1801d4620(chains, 1, .., param_5 = 1, 0x1290, 0x1298, .., &local_98) -> FUN_1801d4850: every bound by the taper
 *      offset of its own Z less R (FUN_1801d34b0), not merged, the split-off pieces discarded;
 *   2. FUN_1801d4620(local_98, 0, D/2, D/2, ..) -> the tool-centre bounds local_90 (the engine's gls_offset / pk_face_tc, the
 *      same substitute as without taper).
 * machine_pocket (FUN_1801d36c0 l.323288) and the control-only finish base (FUN_1801cc170 l.317964 local_168 = param_1) take
 * local_98. Nothing here is checked against Mastercam: no stored operation has tapered walls on. */

/* FUN_1801d4850 l.323044 with param_2 = 1, param_5 = 1: reg (zs aligned) at the depth -> *out (outers ccw, islands cw: the second
   offset works the direction out again, pock_ccw l.323106). 0 + err when the MCCE offset port refuses a bound. */
static int pk_taper_region(const CamParams *p, const Gls *reg, const double *zs, double depth, int facing, double minfil, Gls *out, char *err, size_t en) {
    RmB *src = NULL, *all = NULL, *split = NULL; const char *why = NULL;
    memset(out, 0, sizeof *out);
    for (int i = 0; i < reg->n; i++) rm_b_append(&src, pk_gl2rb(&reg->l[i]));
    int i = 0;
    for (RmB *c = src; c; c = c->next, i++) {
        /* l.323064-323099: an island: inward flag 0, angle param_7 (0x1298; D:1801d48f7 xmm9); an outer: inward flag 1, angle param_6 (0x1290; D:1801d4992 xmm6), for Facing (pock_prm +0x1d2 = 1) flag 0: outward */
        double d = pko_taper_d(p, zs[i], depth, pkd(p, c->isl ? 0x1298 : 0x1290), 1);   /* FUN_1801d34b0 param_3 = r15b = param_2 = 1 (D:1801d48fb): less R */
        pk_isl_orient(p, c);                                                            /* FUN_1801d5870 l.323108 */
        char side = (c->isl || facing) ? (c->ccw ? 'R' : 'L') : (c->ccw ? 'L' : 'R');   /* comp_pock_bnd decomp/mccore_pock2.c l.24-64 */
        RmB *res = NULL;
        if (rm_ccomp_pock(c, &res, d, side, minfil, 1, 1, 0, &why)) {                   /* ccomp_pock(.., min_fil_ang, 1, 1, 0) mccore_pock2.c l.64 */
            snprintf(err, en, "pocket with tapered walls: the wall offset of a bound (comp_pock_bnd, FUN_1801d4850 l.323111): MCCECalcContourOffset port refused: %s: NOT PORTED", why ? why : "");
            rm_b_free_list(&src); rm_b_free_list(&all); rm_b_free_list(&split);
            return 0;
        }
        /* +0x50 = the source bound l.323115-323117; move_split_off_pock_bounds l.323118 (the bounds ccomp_pock marked +1), the rest appended l.323119 */
        while (res) { RmB *n = res->next; res->next = res->prev = NULL; res->par = c; rm_b_append(res->broken ? &split : &all, res); res = n; }
    }
    if (split) {   /* l.323122-323128 */
        double chtol = pkd(p, 0x1228);                                                  /* pock_prm +0 */
        rm_b_bboxs(all); rm_b_bboxs(split);                                             /* FUN_1801d45e0 */
        /* FUN_1801d35f0 l.322423: a split-off outer nested in an island that has another source */
        for (RmB *s = split, *nx; s; s = nx) {
            nx = s->next;
            if (s->isl) continue;
            for (RmB *c = all; c; c = c->next) if (c->isl && !rm_disjoint(s, c) && s->par != c->par && rm_is_ctour_nested(s, c)) { rm_b_del(&split, s); break; }
        }
        /* FUN_1801d3ca0 l.322655: an island nested in or crossing a split-off outer and shorter than it leaves the list */
        for (RmB *s = split; s; s = s->next) {
            if (s->isl) continue;
            Gl gs = pk_rb2gl(s);
            for (RmB *c = all, *nx; c; c = nx) {
                nx = c->next;
                if (!c->isl || rm_disjoint(s, c)) continue;
                Gl gc = pk_rb2gl(c);
                if (pk_isl_nested(&gc, &gs, chtol, rm_nc_tol) && gl_len(&gc) < gl_len(&gs)) rm_b_del(&all, c);   /* is_ctour_nested_or_intersecting (.., pock_prm +0), pock_perim */
                gl_free(&gc);
            }
            gl_free(&gs);
        }
        rm_b_free_list(&split);                                                         /* l.323141: appended only without param_5 */
    }
    for (RmB *c = all; c; c = c->next) {
        Gl g = pk_rb2gl(c);
        if ((gl_area(&g) > 0) == (c->isl != 0)) gl_reverse(&g);
        gls_push(out, g);
    }
    rm_b_free_list(&src); rm_b_free_list(&all);
    return 1;
}
