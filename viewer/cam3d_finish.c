/* cam3d_finish.c - HST 3D finishing toolpaths (op 132): Waterline (UI style 2, kernel style 1). Included by cam2d.c after cam3d_rough.c
 * (the drive mesh, the Z levels, the slice). Decode: formats/hst3d_port_plan.md 7 (cited "wl N"; mk = decomp/mckernel_all.c, hz =
 * decomp/hmm_zplan.c, hp = decomp/hmm_pass0.c, ms = decomp/mcmsurf_all.c).
 * A Waterline pass is the boundary of the tool-centre region at a Z level: EvaWaterlinePassesPlan runs FUN_1803fabf0 with kind 0, whose
 * FUN_180406b00 outputs only the boundary loops (t = 0) of each level's region (hz l.8711-9057, wl 2) - the slice of OptiRough without the
 * clearing.
 * Contact angle limits: the loop vertex filter FUN_18040b6f0, formats/hst3d_port_plan.md 7 (wl_angle_keep below).
 * The weave's box and boundary fill (FUN_18012be70 / FUN_180131810 / FUN_180289d90, plan 7 "The weave box and the boundary fill"): the containment's outline
 * leaves the passes through the clipped contact flag where the passes are the weave's own loops (no flat-radius offset); with an offset (bull tools) the flags
 * reach the passes through McHmm's own Voronoi offset and pass writer (wlvor_pocket, plan 7 "W8"), with Corner rounding FUN_180557f60 when the op asks.
 * Ours (labelled): the tessellator (OCCT, as OptiRough); the loop direction sense (Climb = the material on the right of the motion, a clockwise spindle: the CW / CCW sense of an
 * unreversed Waterline loop is NOT DETERMINED, wl 4); the start of a loop (its point nearest the tool); the links (the SAFE rule:
 * up to the feed plane, across, down - the Waterline Linking Plan 0x141c is NOT DECODED: decoded so far and missing, formats/hst_linker.md);
 * Optimize cut order is cut as By depth. */

/* Tool containment "Tool contact point" K+0x499 = record 0x1730 (FUN_180923f60): styles in the mask 0x196 (Waterline, Scallop, Raster, Spiral, Radial) then build the boundaries through the Cutter Contact
   Detection plan 0x1434 (FUN_18007d7c8 mk l.92875-93074, BoundaryOffsetContactEnable): that plan is NOT DECODED, so the op is refused. */
#define HST_NOFLUTE_WHY "Tool assembly contact (Flute contact off, 0x16cf = 0): the height the drop falls to off the model (the toolbox floor +0x450) is NOT DECODED"
#define HST_CONTACT_WHY "Tool containment by the tool contact point (the Cutter Contact Detection plan 0x1434) is NOT DECODED"
#include "hmm_voronoi.h"    /* mchmm.dll's Voronoi library (the flat-radius offset of the Waterline passes, wlvor_pocket below) */
#include "hmm_pipethin.h"   /* mchmm.dll's pass writer CPipeThinToolpath */
/* the check trimming (Edit Passes to Surfaces 0x141f, cam3d_checktrim.c, included after the drop cutter below) */
typedef struct C3Chk C3Chk;
static C3Chk *c3_chk_open(const McamFile *f, CamOp *op, const uint8_t *rec, uint32_t rn, double defl, double R, double Rc, int metric, char *err, size_t errn);
static const C3Mesh *c3_chk_mesh(const C3Chk *c);
static void c3_chk_close(C3Chk *c);
static int c3_chk_trim(const C3Chk *c, const double *P, int n, int closed, void (*emit)(const double *xyz, int m, void *ctx), void *ctx);
/* The general HST linker's retract link, first approach and final exit with LinkRetractStyle 2 and the vertical leads (formats/hst_linker.md 13, 14;
   hk = decomp/hmm_linker.c, x2 = decomp/hmm_linker_x2.c, ko / ko2 = decomp/mccore_ko*.c, mk = decomp/mckernel_all.c, dis = objdump of mchmm.dll).
   Ported for Raster only (gen_raster sets it):
   - the link kind is 2 (retract) when the XY gap prev end -> next start is >= LinkMaxStayOnSurface (FUN_1801c05c0 hk l.33551);
   - lead-in at the pass start S (FUN_18019e010 hk l.10195-10213): R = LinkVerticalLeadInRadius (record 0x1348, mk l.89259-89270), a = the descent angle
     atan(-Tz / |Txy|) of the pass's 3D tangent T at S (hk l.2984-3013; the clamp +0x1198 = DAT_1809bf1a0 x 0.5 is a run-time constant: ours refuses
     |a| > 87.73 deg, the largest slope measured unclamped on RESTPASSES op 19 - MaxRampAngle +0x11e0 is 0 for Raster as measured: every stored lead-in
     follows the pass slope, hst_linker.md 14.1), dir = Txy / |Txy|, h = R sin a, centre C = S + dir h at S.z + sqrt(R^2 - h^2), top = C - dir R;
   - lead-out at the pass end E (hk l.10165-10185, exit caller hk l.5355-5402): R = LinkVerticalLeadOutRadius (record 0x1350), b = atan(Tz / |Txy|) of
     the end tangent, h = R sin b, C = E - dir h at E.z + sqrt(R^2 - h^2) (the base Z FUN_18019ab50 + param_13 = E.z: FUN_18019ab50 is 0 without a
     horizontal lead, hk l.7014-7060; Raster has none - mckernel's style-4 branch writes no HorizontalLead*, mk l.89903-90011), top = C + dir R;
   - each lead is kept only when FUN_18019a6a0 (hk l.6838, dis 18019a6a0-18019a7c4) returns 0: a connect P2.xy -> P4.xy (lead-in: P2 = top, P4 = S;
     lead-out: P2 = E, P4 = top; P3 = C), layers 0, mode 1, wander off, node base Z = P3.z - 1.1 |P2 - P3|, nodes at max(base, drop + ToolThicknessZ)
     (FUN_1801d2a10 x4 l.57-174), split at XY midpoints while the XY length > StepMinHorizontalChange and the dropped middle is farther than
     StepMachTolerance from the chord (FUN_180007fd0 / FUN_1801cd970, mode 1); 1 when a node is at or above P3.z or the squared distance from P3 to a
     segment (FUN_18005ab30) is < |P2 - P3|^2. Strictly on our own values (N13-21). FUN_1801cea60 may remove nodes afterwards (NOT DECODED): ours
     decides only when the full polyline and the bare chord agree; a lead that is blocked (1: rotation / straight fallback NOT DECODED) or undecided
     keeps the SAFE link (ours, labelled);
   - the arcs are written as lines by mccore FUN_1809187f0 (ko2 l.240-330): n = ceil(sweep / acos((r - 0.45 cut tol) / r)) equal angle steps, cut tol
     = record 0x12a0 (FUN_180919b70 ko l.439);
   - exit: the lead-out arc, a pure-Z move up by LinkRampHeightOffset (feed plane record 0x190; FUN_1801dab60 x2 l.1041-1160; measured on every Raster
     exit), both at the feed; then rapid up to h; approach: rapid to (top, h), rapid down to top + LinkRampHeightOffset, a pure-Z move down to the top and
     the lead-in arc at the plunge rate (measured, hst_linker.md 13.3: approach moves at the plunge rate, exit moves at the feed);
   - h (FUN_1801990c0 x2 l.5881-5935, LinkRetractStyle 2 = record 0x16c4): LinkZClearance = the clearance (record 0x170) when it is on (0x179) and
     absolute (0x178 = 0) (mk l.89177-89205; the tool term FUN_180096984 subtracted there was 0 on every stored style-2 retract, 138 / 138 at the clearance,
     hst_linker.md 13.2), or the layer-0 path height + 1 mm when that is higher; ours: the path (the drop along the straight XY segment between the two
     lifted tops) must stay 1 mm below the clearance, else (the reduce's sampling NOT DECODED) the link keeps SAFE. A clearance that is off or
     incremental (LinkSafeZ's default RoundUp, NOT DECODED) keeps SAFE. */
typedef struct {
    double rin, rout, off, tol, clr, mm;             /* lead radii, LinkRampHeightOffset, the cut tolerance, LinkZClearance, 1 mm in op units */
    double hmin, smt, maxstay;                       /* StepMinHorizontalChange, StepMachTolerance (the test connect's subdivision), LinkMaxStayOnSurface */
    double (*zdrop)(void *ctx, double x, double y); void *zctx;
    int have; double end[3], dir[3];                 /* the previous pass's end and unit 3D tangent there */
    int nret, nsafe, nblock;                         /* links / approaches / exits ported, kept SAFE, of which blocked by the lead test */
    /* the connect's step criteria (FUN_180252d90's block, plan +0xcb8.., set at mchmm.dll 1802eb7b6-1802ebb76) and the linker's tool profile (+0x208,
       read by FUN_180252a70 through +0xcf0: [0] tr0, [1] trc, [3] tfl), mm; zdrop = the linker's layer-0 drop with that profile (gen_raster) */
    double maxh, maxv, tr0, trc, tfl;
    int ncc, ncut, ndrop;                            /* the lead check's calls (FUN_1801ce270 / FUN_1801cc840 / FUN_1801cf7b0), for the trace comparison */
    int npz_ce, npz_drop;                            /* FUN_1801990c0's calls (FUN_1801ce270 / FUN_1801cf7b0) */
    /* the retract leads' attempts when a lead rises (cam3d_hstlink.c): LinkTrimmingStyle (0x1b1c), LinkMaxAllowedTrimming (plan +0x1110, mm), the
       connect's +0x200 = (float) LinkTrimMinOverlapDiff (plan +0x110c, mm); the previous pass (its points, mm) for the exit's FUN_1801dbf80 */
    int tstyle; double maxtrim, r200;
    double *pp; int ppn, ppcap;
    int nfit, nvert, ntrim;                          /* FUN_180171b50 fits, vertical leads emitted, pass trimmings (FUN_1801a44a0 / 1801a47b0) emitted */
    int tin; double tinv;                            /* the next pass starts trimmed by the 3D length tinv (the approach's FUN_1801a44a0, c3_emit_run) */
} C3Link;
typedef struct { double *p; int n, cap; } C3Pl;
static void c3_pl_add(C3Pl *q, double x, double y, double z) {
    if (q->n == q->cap) { q->cap = q->cap ? 2 * q->cap : 16; q->p = realloc(q->p, sizeof(double) * 3 * q->cap); }
    q->p[3 * q->n] = x; q->p[3 * q->n + 1] = y; q->p[3 * q->n + 2] = z; q->n++;
}
static double c3_lk_node(const C3Link *lk, double x, double y, double base) { double d = lk->zdrop(lk->zctx, x, y); return d > base ? d : base; }
static void c3_lk_split(const C3Link *lk, C3Pl *q, const double *a, const double *b, double base, int depth) {
    double dx = b[0] - a[0], dy = b[1] - a[1];
    if (depth < 30 && dx * dx + dy * dy > lk->hmin * lk->hmin) {
        double m[3] = { a[0] + 0.5 * dx, a[1] + 0.5 * dy, 0 }; m[2] = c3_lk_node(lk, m[0], m[1], base);
        double ex = dx, ey = dy, ez = b[2] - a[2], ee = ex * ex + ey * ey + ez * ez;
        double v = ee == 0.0 ? 0.0 : ((m[0] - a[0]) * ex + (m[1] - a[1]) * ey + (m[2] - a[2]) * ez) / ee; v = v < 0 ? 0 : v > 1 ? 1 : v;
        double fx = m[0] - (a[0] + v * ex), fy = m[1] - (a[1] + v * ey), fz = m[2] - (a[2] + v * ez);
        if (fx * fx + fy * fy + fz * fz > lk->smt * lk->smt) { c3_lk_split(lk, q, a, m, base, depth + 1); c3_lk_split(lk, q, m, b, base, depth + 1); return; }
    }
    c3_pl_add(q, b[0], b[1], b[2]);
}
static double c3_seg_d2(const double *P, const double *A, const double *B) {     /* FUN_18005ab30: squared distance point -> segment */
    double ex = B[0] - A[0], ey = B[1] - A[1], ez = B[2] - A[2], ee = ex * ex + ey * ey + ez * ez;
    double t = ee == 0.0 ? 0.0 : ((P[0] - A[0]) * ex + (P[1] - A[1]) * ey + (P[2] - A[2]) * ez) / ee;
    if (t >= 1.0) { double y = P[1] - B[1], x = P[0] - B[0], z = P[2] - B[2]; return y * y + x * x + z * z; }
    if (t <= 0.0) { double y = P[1] - A[1], x = P[0] - A[0], z = P[2] - A[2]; return y * y + x * x + z * z; }
    double x = P[0] - (A[0] + t * ex), y = P[1] - (A[1] + t * ey), z = P[2] - (A[2] + t * ez); return x * x + y * y + z * z;
}
static int c3_lk_hit(const double *pl, int n, const double *P3, double r2) {
    for (int k = 0; k + 1 < n; k++) {
        const double *A = pl + 3 * k, *B = pl + 3 * (k + 1);
        if (P3[2] <= A[2] || P3[2] <= B[2] || c3_seg_d2(P3, A, B) < r2) return 1;
    }
    return 0;
}
/* FUN_18019a6a0: 0 clear, 1 blocked, -1 undecided (the node removal FUN_1801cea60 could change the answer) */
static int c3_lk_test(const C3Link *lk, const double *P2, const double *P3, const double *P4) {
    double x = P2[0] - P3[0], y = P2[1] - P3[1], z = P2[2] - P3[2], r2 = x * x + y * y + z * z;
    double base = P3[2] - sqrt(r2) * 1.1;
    double a[3] = { P2[0], P2[1], c3_lk_node(lk, P2[0], P2[1], base) }, b[3] = { P4[0], P4[1], c3_lk_node(lk, P4[0], P4[1], base) };
    C3Pl q = { 0 }; c3_pl_add(&q, a[0], a[1], a[2]); c3_lk_split(lk, &q, a, b, base, 0);
    double ch[6] = { a[0], a[1], a[2], b[0], b[1], b[2] };
    int full = c3_lk_hit(q.p, q.n, P3, r2), bare = c3_lk_hit(ch, 2, P3, r2);
    if (getenv("MCAM_LK_DEBUG")) { fprintf(stderr, "lk test P2 %.6f %.6f %.6f P3 %.6f %.6f %.6f P4 %.6f %.6f %.6f r2 %.17g base %.6f full %d bare %d\n", P2[0], P2[1], P2[2], P3[0], P3[1], P3[2], P4[0], P4[1], P4[2], r2, base, full, bare);
        for (int k = 0; k < q.n; k++) fprintf(stderr, "   node %.6f %.6f %.6f d2 %.17g\n", q.p[3 * k], q.p[3 * k + 1], q.p[3 * k + 2], k + 1 < q.n ? c3_seg_d2(P3, q.p + 3 * k, q.p + 3 * k + 3) : 0.0); }
    free(q.p);
    return full == bare ? full : -1;
}
/* the arc C + r (cos t u + sin t v), t = 0 .. sw, as mccore writes it (n equal steps, the last point exactly `last`) */
static void c3_lk_arc(const C3Link *lk, MB *b, const double *C, const double *u, const double *v, double r, double sw, const double *last, double feed) {
    double st = acos((r - lk->tol * 0.45) / r); int n = st > 0 ? (int)ceil(sw / st) : 1; if (n < 1) n = 1;
    for (int k = 1; k <= n; k++) {
        if (k == n) { mv(b, 0, last[0], last[1], last[2], feed); break; }
        double t = sw * k / n, c = cos(t), s = sin(t);
        mv(b, 0, C[0] + r * (c * u[0] + s * v[0]), C[1] + r * (c * u[1] + s * v[1]), C[2] + r * (c * u[2] + s * v[2]), feed);
    }
}
/* For Raster (HorizontalLeadIn / OutRadius 0x1afd / 0x1b32 not written by mckernel's style-4 branch, plan default <= 0: linker +0x11f0 / +0x11f8,
   hl l.4904-4907) Mastercam does NOT run FUN_18019e010 / the lead test FUN_18019a6a0: FUN_180194140 (first approach, hk l.2572-3513: FUN_18019e010
   only when +0x11f0 > 0, hk l.2892-3045), FUN_180197b80 (exit, hk l.5300-5400) and FUN_1801a1d70 (retract, hk l.11975-12075, +0x11f8 / +0x11f0) take
   the connect path instead: FUN_1801d1e70 + FUN_1801db250 (approach) / FUN_1801dab60 (exit), whose lead nodes FUN_1801db820 make the vertical lead
   arcs (traced with peloader HMM_TRACE on PARALLELSTEEP op 5: FUN_18019e010 / FUN_18019a6a0 0 calls, FUN_1801db250 1, FUN_1801dab60 1;
   hst_linker.md 19.3). That path's collision steps (FUN_1801d7450: the drop at the connect's layer FUN_1801cf7b0, FUN_1801cece0, FUN_1801e0ee0,
   FUN_1801d9330) are DECODED for the first approach and the final exit (c3_lc_lead below, hst_linker.md 20); per user decision N13-29 a lead
   whose check takes a not-ported branch (a rise), and every retract-link lead (FUN_1801a1d70, not traced), falls back to the SAFE link
   (labelled; counted as "blocked"). c3_lk_test (FUN_18019a6a0) runs for the Waterline retract's vertical leads (wl_retract, hst_linker.md 30). */
/* the lead geometry at a pass point P with unit 3D tangent T (in: the start, descent; out: the end, rise); 0 = refused */
typedef struct C3LeadS { double C[3], top[3], dir[2], a; } C3Lead;
static int c3_lk_lead(const double *P, const double *T, double R, int in, C3Lead *L) {
    double txy = sqrt(T[0] * T[0] + T[1] * T[1]);
    if (!(txy > 0)) return 0;
    L->dir[0] = T[0] * (1.0 / txy); L->dir[1] = T[1] * (1.0 / txy);                     /* as hk l.10137-10142: x (1 / |Txy|) */
    L->a = atan((in ? -T[2] : T[2]) / txy);
    if (fabs(L->a) > 87.73 * M_PI / 180) return 0;                           /* the clamp +0x1198 is a run-time constant: refused beyond the measured range */
    double h = R * sin(L->a), w = R * R - h * h, zc = w > 0 ? sqrt(w) : 0.0, sg = in ? 1.0 : -1.0;
    L->C[0] = P[0] + sg * L->dir[0] * h; L->C[1] = P[1] + sg * L->dir[1] * h; L->C[2] = P[2] + zc;
    L->top[0] = L->C[0] - sg * L->dir[0] * R; L->top[1] = L->C[1] - sg * L->dir[1] * R; L->top[2] = L->C[2];
    return 1;
}
/* The connect path's collision handling for a Raster lead (N13-29's condition; formats/hst_linker.md 20). Traced with peloader HMM_TRACE (:l = the
   connect's node lists) on PARALLELSTEEP op 5 and RESTPASSES op 18: the approach FUN_1801db250 -> FUN_1801db570 -> FUN_1801db820 (x8 l.5-217) puts
   two nodes before the pass start S: the lead top T and the arc centre C (node flag 2), [T, C, S]; the exit FUN_1801dab60 -> FUN_1801daf90 ->
   FUN_1801da4a0 the mirror after the pass end E, [E, C, T]. FUN_1801d7450 (x8 l.587-694) then:
   - drops T (FUN_1801cf7b0 at layer +0x98 = 0: FUN_1801cf500 x4 l.247-294, the CToolDropPos FUN_180222920 from z - ToolThicknessZ, z = max(z, drop +
     ToolThicknessZ) = our zdrop; the +0x88 region path / +0x1280 model are not taken on Raster: FUN_1803a4820 0 calls traced; the +0x7c0
     protect model only with check surfaces: FUN_1803a46d0, in our zdrop, raster_zdrop_mm). If T rises, FUN_1801d7450 removes its neighbour (l.627-636) - that path is NOT DECODED here: SAFE;
   - refines the arc in FUN_1801cece0 (x10 l.1-209, the flag-2 branch): for the arc (a, C, b) (a = the node before C, b = the one after)
     FUN_1801ce270(a, b) (x5 l.1880-2093: the XY gap is not 0 and the connect's +0x88 is 0, so it is FUN_180252d90(+0x50 block, a, b), below) set,
     or the XY angle a-C-b over 90 deg (the dot product of (a - C).xy and (b - C).xy < 0, C not flagged 4), splits at the arc midpoint
     (FUN_1801cc840 x5, its +0x88 = 0 branch); else the arc midpoint (FUN_180060d70 with flag 0) is dropped and a rise splits too.
     FUN_1801cc840 drops the midpoint M (FUN_180060d70(a, C, b, |b - C|, C's flag 4)); if M does not rise it inserts [copy of C, M] before C
     (two arcs a-C'-M, M-C-b, processed again from a); if M rises it moves the arc / sets the "lifted" flag (+0xc8) - NOT DECODED here: SAFE;
   - FUN_1801d0240 removes the nodes flagged 0x80 (none without a rise), FUN_1801e0ee0 (x10 l.214-373) merges consecutive arcs of the same
     orientation (FUN_1801cfda0: both flag 2, the same flag 4) - the split arcs merge back into the one lead arc - and FUN_1801d9330 (x10 l.370-490)
     returns 0 when the highest node is the lead node T followed by the arc whose far end is S / E: then FUN_1801d7450 returns 1, FUN_1801d91e0
     finds the three nodes unchanged and FUN_1801db570 / FUN_1801daf90 keep the lead (the emission FUN_180192510 writes it). Otherwise
     FUN_1801d87b0 / FUN_1801d76a0 (not reached on ops 5 / 18) - NOT DECODED here: SAFE.
   So without a rise the lead is the plain vertical lead arc; with a rise (the DLL's own branches above) the lead keeps SAFE. Checked node by node
   against the DLL (MCAM_LK_DEBUG prints the calls): ops 5 / 18, approach and exit, every FUN_1801ce270 / FUN_1801cc840 / FUN_1801cf7b0 call and
   its midpoint (hst_linker.md 20). */
/* FUN_180252a70 (dis 180252a70-180252d86): the squared deviation used by FUN_180252d90's tolerance test for an XY gap^2 d2 and a Z gap dz >= 0 of the
   linker's tool (+0x208: [0] tr0, [1] trc, [3] tfl) */
static double c3_lc_dev2(const C3Link *lk, double d2, double dz) {
    if (dz == 0.0) return 0.0;
    double r = lk->trc, f = lk->tfl, h = sqrt(d2), L = r == 0.0 ? -1.0 : sqrt(dz * dz + d2), q = r == 0.0 ? 0.0 : r / L;
    double x9 = q * dz + f, x7 = -(q * h), w, x5, x6;
    if (h < r) { double t = r - h; w = sqrt(r * r - t * t); }
    else if (h < f * 2.0 + r) w = r;
    else { double t = lk->tr0 * 2.0 - h; if (!(t > 0.0)) t = 0.0; double u = r - t, v = r * r - u * u; w = v > 0.0 ? sqrt(v) : 0.0; }
    if (dz >= w) { x6 = lk->tr0; x5 = dz - w; }
    else {
        double t = r - dz, w2 = sqrt(r * r - t * t);
        if (h < w2) {
            double v = r * r - (L * 0.5) * (L * 0.5), g = sqrt(v) / L;
            x6 = h * 0.5 + f + g * dz; x5 = dz * 0.5 - g * h;
        } else if (h > f * 2.0 + w2) {
            double h2 = h - f * 2.0, L2 = sqrt(h2 * h2 + dz * dz), v = r * r - (L2 * 0.5) * (L2 * 0.5), g = (v > 0.0 ? sqrt(v) : 0.0) / L2;
            x6 = h2 * 0.5 + f + g * dz; x5 = dz * 0.5 - g * h2;
        } else { x6 = f + w2; x5 = dz - r; }
    }
    double sp = ((x6 - x9) * h + (x5 - x7) * dz) / (h * h + dz * dz);
    if (sp < 0.0 || sp > 1.0) sp = 0.5;
    double a = sp * h + x9 - x6, b = sp * dz + x7 - x5;
    return b * b + a * a;
}
/* FUN_180252d90 (dis 180252d90-180252e95): 1 = the chord a-b needs a split. Block: +0 StepMinHorizontalChange 0x1acf, +0x10 StepMachTolerance 0x1acc,
   +0x18 MaxHorizontalStepChange 0x1bd9, +0x20 StepContactPointChordChange 0x1ace, +0x28 StepMaxVerticalChange 0x1b3b (-1 = off). The +0x20 test
   FUN_1802529d0 compares the two points' contact normals (FUN_1802538d0 of the point's contact code +0x20); the lead nodes carry code 0 (traced on
   ops 5 / 18: FUN_1801db820 / FUN_1801cc840 write 0, the pass ends too), which gives the same normal for both points: never set for them. */
static int c3_lc_chord(C3Link *lk, const double *a, const double *b) {
    lk->ncc++;
    double dx = b[0] - a[0], dy = b[1] - a[1], d2 = dx * dx + dy * dy;
    if (d2 == 0.0) return 0;                                                   /* FUN_1801ce270 x5 l.1884-1889 */
    if (!(d2 > lk->hmin * lk->hmin)) return 0;
    if (lk->maxh != -1.0 && d2 > lk->maxh * lk->maxh) return 1;
    double dz = fabs(a[2] - b[2]);
    if (lk->maxv != -1.0 && dz > lk->maxv) return 1;
    return lk->smt != -1.0 && c3_lc_dev2(lk, d2, dz) > lk->smt * lk->smt;
}
/* FUN_180060d70 (dis 180060d70-180060f00) with FUN_18005c730 (the mean of a and b): the arc a-C-b's midpoint at radius r from C */
static void c3_lc_mid(const double *a, const double *C, const double *b, double r, int up, double *o) {
    double m[3] = { (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5, (a[2] + b[2]) * 0.5 };
    double x = m[0] - C[0], y = m[1] - C[1], z = m[2] - C[2], d = sqrt(x * x + y * y + z * z);
    if (d >= 0.0001) { double s = r / d, t = 1.0 - s; o[0] = m[0] * s + C[0] * t; o[1] = m[1] * s + C[1] * t; o[2] = m[2] * s + C[2] * t; }
    else { o[0] = C[0]; o[1] = C[1]; o[2] = up ? C[2] + r : C[2] - r; }
}
/* FUN_1801cf7b0 at layer 0 (above): 1 when the node rises */
static int c3_lc_drop(C3Link *lk, double *p) {
    lk->ndrop++;
    double z = lk->zdrop(lk->zctx, p[0], p[1]);
    if (z > p[2]) { p[2] = z; return 1; }
    return 0;
}
static double c3_lc_r(const double *b, const double *C) { double y = b[1] - C[1], x = b[0] - C[0], z = b[2] - C[2]; return sqrt(y * y + x * x + z * z); }
/* FUN_1801cece0's flag-2 branch on the arc a-C-b, with FUN_1801cc840's splits; 0 = no node rose, -1 = a rise (NOT DECODED: SAFE) */
static int c3_lc_arc(C3Link *lk, const double *a, const double *C, const double *b, int depth) {
    int dbg = getenv("MCAM_LK_DEBUG") != NULL;
    int split = c3_lc_chord(lk, a, b);
    if (dbg) fprintf(stderr, "lc ce270 (%.17g %.17g %.17g) (%.17g %.17g %.17g) -> %d\n", a[0], a[1], a[2], b[0], b[1], b[2], split);
    if (!split) {
        if ((a[1] - C[1]) * (b[1] - C[1]) + (a[0] - C[0]) * (b[0] - C[0]) < 0.0) split = 1;   /* the 90 deg rule (C not flagged 4) */
        else {
            double m[3]; c3_lc_mid(a, C, b, c3_lc_r(b, C), 0, m);
            double z0 = m[2]; int up = c3_lc_drop(lk, m);
            if (dbg) fprintf(stderr, "lc cf7b0 (%.17g %.17g %.17g) z0 %.17g -> %d\n", m[0], m[1], m[2], z0, up);
            if (!up) return 0;
            split = 1;
        }
    }
    lk->ncut++;
    /* a stack bound only: the DLL's list walk (FUN_1801cece0) has none. An arc whose end lies a hair past the bottom of the circle (the lead-in
       to S of a pass that rises by ~1e-11, RESTPASSES-like scene s4_check_trim on the DLL's passes) keeps failing the 90 deg rule until the
       halved arcs' ends coincide (dot >= 0), some 60-80 halvings down; the bound of 60 cut that short (SAFE) where the DLL emits the lead */
    if (depth > 2000) return -1;
    double M[3]; c3_lc_mid(a, C, b, c3_lc_r(b, C), 0, M);
    double z0 = M[2]; int up = c3_lc_drop(lk, M);
    if (dbg) fprintf(stderr, "lc cc840 (%.17g %.17g %.17g) z0 %.17g -> %d\n", M[0], M[1], M[2], z0, up);
    if (up) return -1;                                                         /* FUN_1801cc840's rise branches: NOT DECODED */
    if (c3_lc_arc(lk, a, C, M, depth + 1)) return -1;
    return c3_lc_arc(lk, M, C, b, depth + 1);
}
/* the lead check (FUN_1801d7450 for a lead connect; in = 1 the approach [T, C, S], else the exit [E, C, T]): 1 = the plain lead arc is kept,
   2 = FUN_1801d9330 returns 1 and FUN_1801d87b0 / FUN_1801d76a0 follow (the attempts, cam3d_hstlink.c) with R = |T - C| and d = |S - T|xy taken
   before the drop (x8 l.598-616). That is the case when T rises (FUN_1801d7450 l.627-636 removes C) and also when the arc's refinement rises:
   FUN_1801cc840's rise branch (x5 l.2191ff, connect +0x88 = 0: the dropped midpoint above the undropped one) hands C and the lifted midpoint to
   the connect's virtual +0x18 (code 5) - the arc centre gives way to the lifted point and +0xc8 ("lifted") is set - so no arc whose far end is S
   follows the highest node and FUN_1801d9330 (x10 l.370-490) returns 1; traced on the synthetic case e1's first approach (formats/hst_linker.md
   30): FUN_1801d8240's fits follow. Which nodes the virtual +0x18 leaves is not needed: FUN_1801d87b0 clears them. */
static int c3_lc_lead_r(C3Link *lk, const double *S, const C3Lead *L, int in) {
    double T[3] = { L->top[0], L->top[1], L->top[2] };
    lk->ndrop++;
    double zt = lk->zdrop(lk->zctx, T[0], T[1]);
    if (getenv("MCAM_LK_DEBUG")) fprintf(stderr, "lc lead %s top (%.17g %.17g %.17g) C (%.17g %.17g %.17g) S (%.17g %.17g %.17g) top drop %.17g\n", in ? "in" : "out",
                                         T[0], T[1], T[2], L->C[0], L->C[1], L->C[2], S[0], S[1], S[2], zt);
    if (zt > T[2]) return 2;
    return (in ? c3_lc_arc(lk, T, L->C, S, 0) : c3_lc_arc(lk, S, L->C, T, 0)) == 0 ? 1 : 2;
}
#include "cam3d_hstlink.c"   /* the retract leads' attempts (FUN_1801d76a0) */
/* FUN_180279cc0 = FUN_18027c660(frag iterator, 1) (x15 l.263-294, x16 l.1-131): the pass's XY length */
static double c3_lk_xylen(const double *xyz, int m) {
    double s = 0.0;
    for (int k = 0; k + 1 < m; k++) { double dx = xyz[3 * k + 3] - xyz[3 * k], dy = xyz[3 * k + 4] - xyz[3 * k + 1]; s += sqrt(dx * dx + dy * dy); }
    return s;
}
/* The retract's leads when one rises (FUN_1801a1d70 hk l.12480-12930 with FUN_1801d69f0 x1 l.112-215; formats/hst_linker.md 24): the first
   FUN_1801d69f0 call runs both leads with the attempts (exit: param_6 = local_1880 = LinkMaxAllowedTrimming > 0 and LinkTrimmingStyle != 0, plan
   +0x110a / +0x110b = style 0 / 1 (hl l.4836-4841), the frag's +0x78 0 and the plan kind not 2..4 (Raster is 1); approach: param_7 = local_1958 the
   same, hk l.11911-11934); param_4 / param_5 = (LinkTrimmingStyle == 2) skip the lead nodes (FUN_1801db570 x7 l.37-44: NOT DECODED here). Then
   (hk l.12738-12905) the exit fits when |+0x1e0| <= LinkMaxAllowedTrimming and < the previous pass's XY length (FUN_180279cc0; dis 1801a3c4a-
   1801a3c7f), the approach likewise with +0x1e8 and the next pass; when one does not, FUN_1801d69f0 runs again with the attempts = the fit flags
   (LinkTrimmingStyle 1: param_4 / param_5 = 0). A trim left nonzero goes to the pass trimming (*tro / *tri, the fit in *so / *si: FUN_1801a44a0 /
   FUN_1801a47b0, hk l.12740-12760 / 12935-12990, after the second call a nonzero approach trim is trimmed too: cVar7 forced 1, l.12895); a side
   with trim 0 is emitted as usual, with the vertical lead (FUN_1801d8630: S / E raised by R, *vin / *vout) when it rose.
   pass / m = the next pass; lk->pp = the previous one. Lo / Li = the rising leads (NULL: not rising). 1 = decoded, 0 = NOT DECODED */
static int c3_lk_attempts(C3Link *lk, const double *xyz, int m, const C3Lead *Lo, const C3Lead *Li, int *vout, int *vin, HlSide *so, HlSide *si, int *tro, int *tri) {
    if (lk->tstyle == 2) return 0;
    int att = lk->maxtrim > 0.0 && lk->tstyle != 0, dbg = getenv("MCAM_LK_DEBUG") != NULL;
    double *q = malloc(sizeof(double) * 3 * (size_t)((lk->ppn > m ? lk->ppn : m) + 1)), to = 0.0, ti = 0.0; int ko = 0, ki = 0, rc = 1;
    for (int pass = 0; pass < 2; pass++) {
        int ao = att, ai = att;
        if (pass) {                                                           /* the fit test, then the second call */
            int fo = fabs(to) <= lk->maxtrim && fabs(to) < c3_lk_xylen(lk->pp, lk->ppn);
            int fi = fabs(ti) <= lk->maxtrim && fabs(ti) < c3_lk_xylen(xyz, m);
            if (dbg) fprintf(stderr, "la fits out %d (trim %.17g) in %d (trim %.17g)\n", fo, to, fi, ti);
            if (fo && fi) break;
            ao = fo; ai = fi;
        }
        to = ti = 0.0; ko = ki = 0;
        if (Lo) {                                                             /* FUN_1801dab60: the previous pass from its end backwards */
            double T[3] = { Lo->top[0], Lo->top[1], Lo->top[2] }, *E = lk->end;
            double R = sqrt((Lo->C[1] - T[1]) * (Lo->C[1] - T[1]) + (Lo->C[0] - T[0]) * (Lo->C[0] - T[0]) + (Lo->C[2] - T[2]) * (Lo->C[2] - T[2]));
            double d = sqrt((E[1] - T[1]) * (E[1] - T[1]) + (E[0] - T[0]) * (E[0] - T[0]));
            if (lk->ppn < 2) { rc = 0; break; }
            int n = hl_pnodes(lk->pp, lk->ppn, 1, (double)2.5f * lk->rout, q);
            ko = hl_rise(lk, q, n, R, d, 1, ao, &to, so);
            if (!ko) { rc = 0; break; }
            to = -to;                                                         /* the exit's +0x1e0 = -v */
        }
        if (Li) {                                                             /* FUN_1801db250: the next pass from its start */
            double T[3] = { Li->top[0], Li->top[1], Li->top[2] };
            double R = sqrt((Li->C[1] - T[1]) * (Li->C[1] - T[1]) + (Li->C[0] - T[0]) * (Li->C[0] - T[0]) + (Li->C[2] - T[2]) * (Li->C[2] - T[2]));
            double d = sqrt((xyz[1] - T[1]) * (xyz[1] - T[1]) + (xyz[0] - T[0]) * (xyz[0] - T[0]));
            int n = hl_pnodes(xyz, m, 0, (double)2.5f * lk->rin, q);
            ki = hl_rise(lk, q, n, R, d, 0, ai, &ti, si);
            if (!ki) { rc = 0; break; }
        }
        if (dbg) fprintf(stderr, "la call %d out %d %.17g in %d %.17g\n", pass + 1, ko, to, ki, ti);
    }
    free(q);
    if (rc != 1) return rc;
    *vout = ko == 2; *vin = ki == 2; *tro = ko == 1 && to != 0.0; *tri = ki == 1 && ti != 0.0;
    return 1;
}
/* One rising lead of the first approach (FUN_180194140 hk l.3091-3168, FUN_1801db250) or of the final exit (FUN_180197b80 hk l.5461-5533,
   FUN_1801dab60): the first call with the attempts flag - approach: LinkTrimmingStyle 1 and LinkMaxAllowedTrimming > 0 (plan +0x110b, hk
   l.3092-3098; plan kinds 2..4 never: Raster is 1), exit: LinkTrimmingStyle != 0 and LinkMaxAllowedTrimming > 0 (+0x110a, hk l.5461-5470); both
   without the frag's +0x78 (never on a Raster pass). The trim (approach +0x1e8, exit +0x1e0) fits when |trim| <= LinkMaxAllowedTrimming and
   < the pass's XY length (FUN_180279cc0; approach hk l.3131-3138, exit dis 1801988e0-180198907; the connect's +0x210 / +0x208 neighbour frag is
   empty on Raster); else the connect's nodes are cleared and the second call runs without the attempts (hk l.3140-3165 / 5512-5533), whose
   FUN_1801d76a0 makes the vertical lead FUN_1801d8630. A fitting nonzero trim goes to the pass trimming FUN_1801a44a0 / FUN_1801a47b0 (hk
   l.3196-3205 / 5546-5559). LinkTrimmingStyle 2 (the lead nodes skipped: FUN_1801db250 / FUN_1801dab60 param_4) is NOT DECODED.
   pass = the next pass (approach) / the last one (exit). 1 = the vertical lead, 3 = a fitting trim (the fit in *sd), 0 = NOT DECODED */
static int c3_lk_one(C3Link *lk, const double *xyz, int m, const C3Lead *L, int ex, HlSide *sd) {
    if (lk->tstyle == 2 || m < 2) return 0;
    int att = lk->maxtrim > 0.0 && (ex ? lk->tstyle != 0 : lk->tstyle == 1), dbg = getenv("MCAM_LK_DEBUG") != NULL;
    const double *T = L->top, *S = ex ? xyz + 3 * (m - 1) : xyz;
    double R = c3_lc_r(L->C, T), d = sqrt((S[1] - T[1]) * (S[1] - T[1]) + (S[0] - T[0]) * (S[0] - T[0])), t = 0.0;
    double *q = malloc(sizeof(double) * 3 * (size_t)(m + 1));
    int n = hl_pnodes(xyz, m, ex, (double)2.5f * (ex ? lk->rout : lk->rin), q);
    int k = hl_rise(lk, q, n, R, d, ex, att, &t, sd);
    if (k == 1) {
        int fit = fabs(t) <= lk->maxtrim && fabs(t) < c3_lk_xylen(xyz, m);
        if (dbg) fprintf(stderr, "la %s fits %d (trim %.17g)\n", ex ? "final exit" : "first approach", fit, ex ? -t : t);
        if (fit) { free(q); return 3; }
        k = hl_rise(lk, q, n, R, d, ex, 0, &t, NULL);                         /* the second call: FUN_1801d8630 */
    }
    free(q);
    return k == 2 ? 1 : 0;
}
/* The general HST linker's stay-down link with a surface ramp (EvaGeneralLinkerPlan, mchmm.dll; formats/hst_linker.md 12, 21; hk = decomp/hmm_linker.c,
   x1 / x3 / x5 / x6 = decomp/hmm_linker_x1.c ..., hl = decomp/hmm_link.c, mk = decomp/mckernel_all.c). Ported for Raster only (gen_raster sets it):
   - stay down (link kind 1) when the XY distance from the previous pass's end to the next pass's start is < LinkMaxStayOnSurface (FUN_1801c05c0 hk
     l.33551; for plan kinds other than Waterline no adjacency test unless both passes are region passes 0x13cc / 0x13fa, l.33611-33617);
   - kind 1 -> 3 (surface ramp) with SurfaceRampStyle 1 (FUN_1801c3dd0 x1, hl l.5009): Raster's 0x1a81 = (record 0x16d8 != 1) (mk l.89903-89918);
     measured: all 473 stay-down links of PARALLELSTEEP.mcam op 5 are kind 3 (the curve below), none is SurfaceRampExludeSameLevel's straight kind 1;
   - the kind-3 connect (FUN_1801d1e70 -> FUN_1801d2110 x3 l.2830ff) runs on the cubic Bezier [P0, P0 + T0 L, P1 - T1 L, P1], T0 / T1 = the unit 3D
     tangents of the previous pass at its end and of the next pass at its start (FUN_18027ad40), L = SurfaceRampTangentFactor 0x1b61 x (SurfaceRamp-
     ScaleByGapSize 0x1eca ? |P0 P1| : 1); Raster's defaults (FUN_1803adb40 hl l.13912-13926, 13931-13935; mckernel sets neither for kernel style 4:
     its block that does is reached for style 6 only, mckernel.dll 180076fb1) = EvaluateR(MachiningPasses, MachStepover) and false;
   - the nodes start at t = 0 / 1 (FUN_1801d2bd0 x1); the TBB reduce (FUN_180006910 x3 -> FUN_180007fd0 x5 l.507-620) walks the node pairs (a, b):
     FUN_1801cf230 (x5 l.3029-3140) = FUN_1801ce270(a, b) (the connect's chord test: FUN_180252d90 on Raster, c3_lc_chord) and, when set in mode 3,
     FUN_1801ce270 again on the curve points at a's / b's t (the undropped nodes); when not set, with LinkSurfaceRampSmooth 0x1b20 (Raster default
     true) the curve point at the middle t farther in XY from the line through the curve points at ta / tb than SurfaceRampMaxDeviation 0x1a80
     (plan +0x1208 -> connect +0xd0; Raster default 0.005 mm, FUN_1803adb40; mckernel writes the cut tolerance only when it is below 0.005 mm, mk
     l.89993-90001). Then, when that is set or the nodes' XY gap^2 > StepMinHorizontalChange^2 (block +8), FUN_1801cd970 (x6 l.65-300): not set and
     |ta - tb| < FLT_EPSILON -> no split; else the node at t = ta / 2 + tb / 2 (float) is the curve point dropped at the connect's layer (FUN_1801cf7b0
     at +0x98 = 0: z = max(z, the linker's drop), c3_lc_drop); not set yet, the XY gap^2 > StepMinHorizontalChange^2 and the dropped node farther
     (3D, the chord segment of the dropped nodes, FUN_180059560) than StepMachTolerance (block +0x10) -> set. Set: the node goes in between
     (FUN_180007eb0 -> FUN_1801d0b20) and the walk goes on from a (depth first); FUN_180007eb0 keeps the highest node, the largest |dropped z - curve
     z| and a "lifted" flag (dropped z above the curve point's);
   - after the reduce (FUN_1801d2bd0 x1 l.1259-1330, mode 3): FUN_1801d3a60 / 1801d4270 / 1801d3c20 / 1801d39e0 compare and undo the nodes' wander
     offsets (node +0x44 bit 1, +0x48..; x3 l.736-1060): those are set only by the connect's virtual +0x28, which FUN_1801cd970 calls only with
     connect +0xb0 (LinkAllowWandering, default false, not written by mckernel for Raster), so without a lift they leave the nodes unchanged;
     FUN_1801d0240 removes the nodes flagged 0x80 (none); FUN_1801cea60's node removal: no change on the 477 / 371 traced links of ops 5 / 18;
   - kind 1 (both passes on the same raster line, frag +0x70, with SurfaceRampExludeSameLevel +0x1234 default true: FUN_1801c3dd0 x1 l.2014-2018)
     is the mode-1 connect: the same reduce without the mode-3 curve tests (FUN_1801cf230 = FUN_1801ce270 on the nodes), the new node at the XY
     midpoint (FUN_18000b4e0, 0.5) at the mean of the two nodes' Z (the connect's wander flag +0xa0 set: both passes' +0x78; traced: FUN_1801deaf0
     runs for these links) dropped at layer +0x98 (FUN_1801cd970 l.188-205), split when farther than StepMachTolerance from the chord; then the
     wander FUN_1801deaf0 (x3) leaves a 2-node path alone (returns 1 for 2 nodes) - so an unsplit kind-1 link is the straight move P0 -> P1; a
     split one goes to the wander FUN_1801dde40 (NOT DECODED: SAFE);
   - FUN_180199220 (hk l.6040-6400): after FUN_1801d2bd0, when the connect's largest rise (+0x38, the reduce's |dropped z - curve z| of the
     inserted nodes, FUN_180006910 x3 l.120-125 / FUN_180007eb0) is above SurfaceRampMaxVerticalChange 0x1cfb (Raster default 2.1 x
     StepMachTolerance, FUN_1803adb40; mckernel writes it for kernel style 6 only) the pass-extension loop FUN_18019a310 (only when neither frag
     has +0x78; c3_staydown below: the extension loop, a mode-1 retry, the retract FUN_1801a1d70 = c3_link); else, the path length > 0, the
     connect's nodes are the link at the feed (FUN_180192510, tag 0x13d4) - lifted nodes included (traced on a synthetic valley: 119 / 119
     mode-3 links emitted with their lifted nodes, flag 0x40 on every node so FUN_1801cea60 removes none).
   Checked: the stored links of PARALLELSTEEP.mcam op 5 = these nodes, 473 / 473, to 4.4e-7 in (formats/hst_linker.md 12.3); every FUN_1801ce270 /
   FUN_1801cf7b0 call of the stay-down links of ops 5 / 18 against mchmm.dll (HMM_TRACE, formats/hst_linker.md 21).
   NOT DECODED, so such a link keeps the SAFE rule (labelled; user decision N13-29): a split kind-1 link (the wander FUN_1801dde40), also as the
   rise branch's mode-1 retry or the extension loop's last connect. */
typedef struct {
    double maxstay, tf, maxdev, maxvert;              /* LinkMaxStayOnSurface, SurfaceRampTangentFactor (x 1: ScaleByGapSize off), SurfaceRampMaxDeviation,
                                                         SurfaceRampMaxVerticalChange (mm) */
    C3Link *cn;                                       /* the connect's step block (FUN_180252d90), the linker's tool and drop (c3_lc_chord / c3_lc_drop) */
    int have, key; double end[3], dir[3];            /* the previous pass: its raster line (frag +0x70), its end and its unit tangent there */
    int nstay, nsafe, nrise, nkind1;                 /* links made stay-down / kept SAFE (of which: a rise above SurfaceRampMaxVerticalChange, a split kind 1) */
    long ncf230, nce270, ncf7b0;                     /* calls (FUN_1801cf230 / FUN_1801ce270 / FUN_1801cf7b0), for the trace comparison */
    double dia, dir0[3];                             /* ToolCutterDiameter (mm); the previous pass's end tangent unnormalised (FUN_18027ad40's vector) */
    int nretry, nret, next;                          /* rises: a straight mode-1 retry emitted / handed to the retract / the extension loop's link emitted */
    long ncf7b0x;                                    /* the extension drops (FUN_1801cf7b0 from 18019a59f) */
    int nwander;                                     /* mode-1 paths through the wander FUN_1801dde40 */
} C3StayDown;
static void c3_bez(const double c[4][3], double t, double *o) {
    double u = 1 - t, b0 = u * u * u, b1 = 3 * u * u * t, b2 = 3 * u * t * t, b3 = t * t * t;
    for (int i = 0; i < 3; i++) o[i] = b0 * c[0][i] + b1 * c[1][i] + b2 * c[2][i] + b3 * c[3][i];
}
typedef struct WlRamp WlRamp;
typedef struct { double (*c)[3]; float *t; double *p; int n, cap, rise, dbg, mode; double maxrise; C3StayDown *sd; const WlRamp *wr; int lift;
                 double *rm; int nrm;           /* rm: mode-1 nodes put in whose drop left them where they were (node flag 0x80, c3_sd_hwander) */
                 int wnd; double woff, wz0, *w; long nwnd; } C3Sd;   /* wnd: the node wander (c3_sd_wander); w: per node the wandered flag and offset */
static void wl_ramp_pt(const WlRamp *w, double t, double *o);
/* the connect's curve point at t (its virtual +8, FUN_1801dd430 hlc l.32-180): the cubic Bezier of the control points (Raster: no pieces, neither frag
   closed), else the Waterline surface ramp's blend of the two pieces (wl_ramp_pt) */
static void c3_sd_pt(const C3Sd *s, double t, double *o) { if (s->wr) wl_ramp_pt(s->wr, t, o); else c3_bez((const double (*)[3])s->c, t, o); }
/* a node: p = x y z, with the wander (s->wnd) also p[3] = the wandered flag (node +0x44 bit 0) and p[4..6] = its offset (+0x48..+0x58) */
static void c3_sd_add(C3Sd *s, float t, const double *p) {
    if (s->n == s->cap) { s->cap = s->cap ? 2 * s->cap : 32; s->t = realloc(s->t, sizeof(float) * s->cap); s->p = realloc(s->p, sizeof(double) * 3 * s->cap);
                          if (s->wnd) s->w = realloc(s->w, sizeof(double) * 4 * s->cap); }
    s->t[s->n] = t; memcpy(s->p + 3 * s->n, p, sizeof(double) * 3);
    if (s->wnd) memcpy(s->w + 4 * s->n, p + 3, sizeof(double) * 4);
    s->n++;
}
static void c3_sd_wander(C3Sd *s, const double *C, double *N, double t, const double *a, const double *b);
static int c3_sd_ce270(C3Sd *s, const char *k, const double *a, const double *b) {
    C3StayDown *sd = s->sd; sd->nce270++;
    int r = c3_lc_chord(sd->cn, a, b); sd->cn->ncc--;                         /* (counted here, not in the lead check's count) */
    if (s->dbg) fprintf(stderr, "%s %s (%.17g %.17g %.17g) (%.17g %.17g %.17g) -> %d\n", s->dbg == 1 ? "sd" : "sx", k, a[0], a[1], a[2], b[0], b[1], b[2], r);
    return r;
}
static void c3_sd_split(C3Sd *s, float ta, const double *a, float tb, const double *b, int depth);
/* the mode-1 connect's FUN_1801cd970 (x6 l.172-260) after FUN_1801cf230 = FUN_1801ce270 (split): the XY midpoint at the nodes' mean Z, dropped */
static void c3_sd_line(C3Sd *s, const double *a, const double *b, int split, int depth) {
    C3StayDown *sd = s->sd;
    double gx = a[0] - b[0], gy = a[1] - b[1], g2 = gx * gx + gy * gy, h2 = sd->cn->hmin * sd->cn->hmin;
    if (!split && !(g2 > h2)) { c3_sd_add(s, -1.0f, b); return; }
    double M[3] = { a[0] * 0.5 + b[0] * 0.5, a[1] * 0.5 + b[1] * 0.5, 0 };
    M[2] = a[2] == b[2] ? a[2] : b[2] * 0.5 + a[2] * 0.5;                    /* (FUN_1801cd970 l.193-197) */
    double z0 = M[2]; sd->ncf7b0++; sd->cn->ndrop--;
    int up = c3_lc_drop(sd->cn, M);
    if (s->dbg) fprintf(stderr, "%s cf7b0 (%.17g %.17g %.17g) z0 %.17g -> %d\n", s->dbg == 1 ? "sd" : "sx", M[0], M[1], M[2], z0, up);
    if (!split && g2 > h2) {
        double ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2], ee = ex * ex + ey * ey + ez * ez;
        double v = ee == 0.0 ? 0.0 : ((M[0] - a[0]) * ex + (M[1] - a[1]) * ey + (M[2] - a[2]) * ez) / ee; v = v < 0 ? 0 : v > 1 ? 1 : v;
        double fx = M[0] - (a[0] + v * ex), fy = M[1] - (a[1] + v * ey), fz = M[2] - (a[2] + v * ez);
        split = sqrt(fx * fx + fy * fy + fz * fz) > sd->cn->smt;
    }
    if (!split) { c3_sd_add(s, -1.0f, b); return; }
    if (up) { s->rise = 1; if (M[2] - z0 > s->maxrise) s->maxrise = M[2] - z0; }
    else {                                                                    /* FUN_180007eb0 (x6 l.306-345): the node virtual +0x20 with code 6 when the
                                                                                 dropped Z equals the curve's (code 5 otherwise): flag 0x80, removed by
                                                                                 FUN_1801d0240 after the reduce (traced on the ridge case w3) */
        s->rm = realloc(s->rm, sizeof(double) * 3 * (size_t)(s->nrm + 1)); memcpy(s->rm + 3 * s->nrm, M, sizeof M); s->nrm++;
    }
    if (depth > 200) { s->rise = 1; c3_sd_add(s, -1.0f, b); return; }       /* (a stack bound) */
    c3_sd_split(s, -1.0f, a, -1.0f, M, depth + 1);
    c3_sd_split(s, -1.0f, M, -1.0f, b, depth + 1);
}
/* the pair (ta, a) - (tb, b) of nodes (a / b: the dropped node points); b is added after the nodes put in between */
static void c3_sd_split(C3Sd *s, float ta, const double *a, float tb, const double *b, int depth) {
    C3StayDown *sd = s->sd;
    sd->ncf230++;
    int split = c3_sd_ce270(s, "ce270", a, b);                                 /* FUN_1801cf230: the nodes */
    if (s->mode == 1) { c3_sd_line(s, a, b, split, depth); return; }
    if (split) { double ca[3], cb[3]; c3_sd_pt(s, (double)ta, ca); c3_sd_pt(s, (double)tb, cb);
                 split = c3_sd_ce270(s, "ce270c", ca, cb); }                    /* mode 3: the curve points at ta / tb */
    float tm = ta; if (ta != tb) tm = ta * 0.5f + tb * 0.5f;                  /* the float mean */
    if (!split) {                                                             /* LinkSurfaceRampSmooth: the XY distance of the middle curve point from the line */
        double m[3], ca[3], cb[3]; c3_sd_pt(s, (double)tm, m);
        c3_sd_pt(s, (double)ta, ca); c3_sd_pt(s, (double)tb, cb);
        double dx = ca[0] - cb[0], dy = ca[1] - cb[1], dd = dx * dx + dy * dy, w = dd == 0.0 ? 0.5 : (dy * (m[1] - cb[1]) + dx * (m[0] - cb[0])) / dd;
        double ey = m[1] - (dy * w + cb[1]), ex = m[0] - (dx * w + cb[0]);
        split = sqrt(ey * ey + ex * ex) > sd->maxdev;
    }
    double gx = a[0] - b[0], gy = a[1] - b[1], g2 = gx * gx + gy * gy, h2 = sd->cn->hmin * sd->cn->hmin;
    if (!split && !(g2 > h2)) { c3_sd_add(s, tb, b); return; }                /* FUN_180007fd0: FUN_1801cd970 not called */
    if (!split && fabsf(ta - tb) < 1.1920929e-07f) { c3_sd_add(s, tb, b); return; }   /* FUN_1801cd970 mode 3 */
    double M[7] = { 0 }; c3_sd_pt(s, (double)tm, M);
    double z0 = M[2], C[3] = { M[0], M[1], M[2] }; sd->ncf7b0++; sd->cn->ndrop--;
    int up = c3_lc_drop(sd->cn, M);                                           /* FUN_1801cf7b0 at the connect's layer */
    if (up) s->lift = 1;
    if (s->dbg) fprintf(stderr, "%s cf7b0 (%.17g %.17g %.17g) z0 %.17g -> %d\n", s->dbg == 1 ? "sd" : "sx", M[0], M[1], M[2], z0, up);
    if (s->wnd) c3_sd_wander(s, C, M, (double)tm, a, b);                     /* FUN_1801cd970 l.230-232: the connect's virtual +0x28 (connect +0xb0, mode 3) */
    if (!split && g2 > h2) {                                                  /* the 3D distance of the dropped node from the chord a-b (FUN_180059560) */
        double ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2], ee = ex * ex + ey * ey + ez * ez;
        double v = ee == 0.0 ? 0.0 : ((M[0] - a[0]) * ex + (M[1] - a[1]) * ey + (M[2] - a[2]) * ez) / ee; v = v < 0 ? 0 : v > 1 ? 1 : v;
        double fx = M[0] - (a[0] + v * ex), fy = M[1] - (a[1] + v * ey), fz = M[2] - (a[2] + v * ez);
        split = sqrt(fx * fx + fy * fy + fz * fz) > sd->cn->smt;
    }
    if (!split) { c3_sd_add(s, tb, b); return; }
    if (M[2] > z0) s->rise = 1;                                               /* FUN_180007eb0 (x6 l.306-360): the lifted flag (node z above the curve z), */
    if (fabs(M[2] - z0) > s->maxrise) s->maxrise = fabs(M[2] - z0);          /* the largest |node z - curve z| (+0x38), on the node as FUN_1801cd970 left it */
    if (depth > 200) { s->rise = 1; c3_sd_add(s, tb, b); return; }           /* (not reached: the float t stops the subdivision; a stack bound) */
    c3_sd_split(s, ta, a, tm, M, depth + 1);
    c3_sd_split(s, tm, M, tb, b, depth + 1);
}
/* the wander of a mode-1 connect's node path (FUN_1801deaf0 / FUN_1801dde40, cam3d_hstlink.c hl_wander; c3_sd_wander below is the node wander FUN_1801d4990): its two end nodes carry 0x40, the nodes
   the reduce put in none; 1 = done (s->n updated), 0 = NOT DECODED */
static int c3_sd_hwander(C3Sd *s) {
    if (s->nrm) {                                                             /* FUN_1801d0240 (x10): the nodes flagged 0x80 go first */
        int m = 0;
        for (int i = 0; i < s->n; i++) {
            int del = 0; const double *q = s->p + 3 * i;
            for (int k = 0; k < s->nrm && !del; k++) del = i > 0 && i < s->n - 1 && !memcmp(q, s->rm + 3 * k, sizeof(double) * 3);
            if (!del) { if (m != i) memmove(s->p + 3 * m, q, sizeof(double) * 3); m++; }
        }
        if (s->dbg) fprintf(stderr, "%s d0240 %d -> %d\n", s->dbg == 1 ? "sd" : "sx", s->n, m);
        s->n = m; s->nrm = 0;
    }
    if (s->n <= 2) return 1;
    unsigned char *fl = calloc((size_t)s->n, 1); fl[0] = fl[s->n - 1] = 0x40;
    int n0 = s->n, m = hl_wander(s->p, s->n, fl); free(fl);
    if (s->dbg) fprintf(stderr, "%s wander %d -> %d\n", s->dbg == 1 ? "sd" : "sx", n0, m);
    if (!m) return 0;
    s->n = m; s->sd->nwander++;
    return 1;
}
/* The node wander (the connect's virtual +0x28 = FUN_1801d4990, decomp/hmm_linker_x14.c l.38-457; called by FUN_1801cd970 x6 l.230-232 for every new
   node of a mode-3 connect with +0xb0 = LinkAllowWandering 0x1eb1 set) on the dropped node N (7 doubles, c3_sd_add) at float t, C = the curve point at t
   (undropped), a / b = the segment's end nodes:
   - N = C (x, y and z equal, FUN_1800082c0 exact) -> unchanged;
   - the direction: both a and b wandered (+0x44 bit 0) -> the unit sum of their XY offsets; else FUN_1801d4840 (x14 l.1-37: the curve at t -+ 0.001 in
     double, clamped to 0 / 1, the unit (-dy, dx) of the difference), and when that is 0 the unit (-(a.y - b.y), a.x - b.x);
   - P+ / P- = N.xy +- direction x LinkWanderInitialOffset (connect +0xb8 = plan +0x1180) at z min(connect +0x80, C.z), each dropped (FUN_1801cf7b0
     at the node's layer); N below both (strict) -> unchanged;
   - else bisect from M = N: while |P+ - P-|^2 > the step block's +8 (StepMinHorizontalChange^2): the segment (P+, M) or (M, P-) farther from C
     (FUN_18005ab30, squared) is dropped - d(C, M P-) <= d(C, P+ M) -> P+ = M, else P- = M; M = the XY mean (P+ when both XY equal) at the mean Z
     (P+.z when equal), dropped; after 1000 rounds it stops when the gap stops shrinking (the DLL's "WanderedPosition abandoned" log; not traced);
   - E = P- when P-.z <= P+.z, else P+; E.xy != N.xy -> the node moves to E with the flag set and the offset E - N (x14 l.420-436). */
static void c3_sd_wander(C3Sd *s, const double *C, double *N, double t, const double *a, const double *b) {
    C3StayDown *sd = s->sd;
    if (N[0] == C[0] && N[1] == C[1] && N[2] == C[2]) return;
    double dx, dy;
    if (a[3] != 0.0 && b[3] != 0.0) {
        double sy = a[5] + b[5], sx = a[4] + b[4], l = sqrt(sx * sx + sy * sy); if (l == 0.0) l = 1.0;
        dy = (1.0 / l) * sy; dx = (1.0 / l) * sx;
    } else {
        double t0 = t - 0.001, t1 = t + 0.001, A[3], B[3];
        c3_sd_pt(s, t0 <= 0.0 ? 0.0 : t0, A); c3_sd_pt(s, t1 >= 1.0 ? 1.0 : t1, B);
        double ux = B[0] - A[0], uy = -(B[1] - A[1]), l = sqrt(uy * uy + ux * ux); if (l == 0.0) l = 1.0;
        dy = (1.0 / l) * ux; dx = uy * (1.0 / l);
        if (sqrt(dy * dy + dx * dx) == 0.0) {
            double ey = a[1] - b[1], ex = a[0] - b[0], m = sqrt(-ey * -ey + ex * ex); if (m == 0.0) m = 1.0;
            dy = (1.0 / m) * ex; dx = (1.0 / m) * -ey;
        }
    }
    double z0 = s->wz0; if (C[2] < z0) z0 = C[2];
    double off = s->woff;
    double Pp[3] = { N[0] + dx * off, N[1] + dy * off, z0 }, Pm[3] = { N[0] - dx * off, N[1] - dy * off, z0 };
    sd->cn->ndrop -= 2; c3_lc_drop(sd->cn, Pp); c3_lc_drop(sd->cn, Pm);
    if (N[2] < Pp[2] && N[2] < Pm[2]) return;
    double M[3] = { N[0], N[1], N[2] }, h2 = sd->cn->hmin * sd->cn->hmin, last = 0.0;
    for (int it = 1;; it++) {
        double gz = Pp[2] - Pm[2], gy = Pp[1] - Pm[1], gx = Pp[0] - Pm[0], g = gy * gy + gx * gx + gz * gz;
        if (!(g > h2)) break;
        double d1 = c3_seg_d2(C, Pp, M), d2 = c3_seg_d2(C, M, Pm);
        if (d2 <= d1) memcpy(Pp, M, sizeof M); else memcpy(Pm, M, sizeof M);
        if (Pp[0] == Pm[0] && Pp[1] == Pm[1]) { M[0] = Pp[0]; M[1] = Pp[1]; }
        else { M[0] = Pp[0] * 0.5 + Pm[0] * 0.5; M[1] = Pp[1] * 0.5 + Pm[1] * 0.5; }
        M[2] = Pp[2] == Pm[2] ? Pp[2] : Pm[2] * 0.5 + Pp[2] * 0.5;
        sd->cn->ndrop--; c3_lc_drop(sd->cn, M);
        double cz = Pp[2] - Pm[2], cy = Pp[1] - Pm[1], cx = Pp[0] - Pm[0], gap = sqrt(cy * cy + cx * cx + cz * cz);
        if (it == 1000) last = gap;
        else if (it > 1000) { int stop = gap <= last; last = gap; if (stop) break; }
    }
    const double *E = Pm[2] <= Pp[2] ? Pm : Pp;
    s->nwnd++;
    if (s->dbg) fprintf(stderr, "sd wander (%.17g %.17g %.17g) -> (%.17g %.17g %.17g)\n", N[0], N[1], N[2], E[0], E[1], E[2]);
    if (E[0] == N[0] && E[1] == N[1]) { N[3] = 0.0; return; }
    N[3] = 1.0; N[4] = E[0] - N[0]; N[5] = E[1] - N[1]; N[6] = E[2] - N[2];
    N[0] = E[0]; N[1] = E[1]; N[2] = E[2];
}
/* The mode-3 checks after the reduce (FUN_1801d2bd0 x1 l.1271-1281), on the nodes first .. last:
   - FUN_1801d3a60 (x14 l.458-532): the mean 3D distance of the nodes from the curve at their t, [0] at the positions without the wander offsets, [1] with;
   - FUN_1801d4270 (x2 l.248-440; dis 1801d4270-1801d4820): over the node triples (a, b, c) the largest cosine of the angle at b (the unit vectors b->a
     and b->c), [0] without the offsets, [1] with (from -2.0);
   - when ([0] = 0, [1] = 0, [0] / [1] < LinkWanderDistanceRatioLo or ([0] / [1] < LinkWanderDistanceRatioHi and the cosine without < the cosine with))
     FUN_1801d3c20 (x3 l.808-1035): the path lengths from the first node (without / with), each node's Z raised to the line first -> last (the current
     end nodes, the node's projection clamped to the segment) where that is higher; the length without < the length with -> FUN_1801d39e0 (x14
     l.534-560) takes every offset back. LinkWanderDistanceRatioLo / Hi 0x1b2e / 0x1b2f default 2.0 / 30.0 (FUN_1803a9720's neighbour hl
     l.12295-12309; connect +0x188 / +0x190). Returns 1 when the wander was undone. */
static int c3_sd_wcheck(C3Sd *s) {
    int n = s->n; if (n < 2) return 0;
    const double *P = s->p, *W = s->w;
    #define WU(i, k) (W[4 * (i)] != 0.0 ? P[3 * (i) + (k)] - W[4 * (i) + 1 + (k)] : P[3 * (i) + (k)])
    double du = 0.0, dw = 0.0;
    for (int i = 0; i < n; i++) {
        double c[3]; c3_sd_pt(s, (double)s->t[i], c);
        double u0 = WU(i, 0), u1 = WU(i, 1), u2 = WU(i, 2);
        du += sqrt((c[1] - u1) * (c[1] - u1) + (c[0] - u0) * (c[0] - u0) + (c[2] - u2) * (c[2] - u2));
        dw += sqrt((c[1] - P[3 * i + 1]) * (c[1] - P[3 * i + 1]) + (c[0] - P[3 * i]) * (c[0] - P[3 * i]) + (c[2] - P[3 * i + 2]) * (c[2] - P[3 * i + 2]));
    }
    du = (1.0 / n) * du; dw = (1.0 / n) * dw;
    double cu = -2.0, cw = -2.0;
    for (int i = 1; i + 1 < n; i++) {
        double v[3][3], u[3][3];
        for (int k = 0; k < 3; k++) for (int j = 0; j < 3; j++) { v[k][j] = P[3 * (i - 1 + k) + j]; u[k][j] = WU(i - 1 + k, j); }
        double ax = v[0][0] - v[1][0], ay = v[0][1] - v[1][1], az = v[0][2] - v[1][2], la = sqrt(ay * ay + ax * ax + az * az); if (la == 0.0) la = 1.0;
        double cx = v[2][0] - v[1][0], cy = v[2][1] - v[1][1], cz = v[2][2] - v[1][2], lc = sqrt(cy * cy + cx * cx + cz * cz); if (lc == 0.0) lc = 1.0;
        la = 1.0 / la; lc = 1.0 / lc;
        double kw = lc * cy * la * ay + lc * cx * la * ax + lc * cz * la * az;
        double bx = u[0][0] - u[1][0], by = u[0][1] - u[1][1], bz = u[0][2] - u[1][2], lb = sqrt(by * by + bx * bx + bz * bz); if (lb == 0.0) lb = 1.0;
        double ex = u[2][0] - u[1][0], ey = u[2][1] - u[1][1], ez = u[2][2] - u[1][2], le = sqrt(ey * ey + ex * ex + ez * ez); if (le == 0.0) le = 1.0;
        lb = 1.0 / lb; le = 1.0 / le;
        double ku = le * ey * lb * by + le * ex * lb * bx + le * ez * lb * bz;
        if (cw <= kw) cw = kw;
        if (cu <= ku) cu = ku;
    }
    int undo = 0;
    if (du == 0.0 || dw == 0.0 || du / dw < 2.0 || (du / dw < 30.0 && cu < cw)) {
        const double *A = P, *B = P + 3 * (n - 1);
        double ex = B[0] - A[0], ey = B[1] - A[1], ez = B[2] - A[2], ee = ey * ey + ex * ex + ez * ez;
        int same = A[0] == B[0] && A[1] == B[1] && A[2] == B[2];
        double pu[3] = { WU(0, 0), WU(0, 1), WU(0, 2) }, pw[3] = { A[0], A[1], A[2] }, lu = 0.0, lw = 0.0;
        for (int i = 1; i < n; i++) {
            double U[3] = { WU(i, 0), WU(i, 1), WU(i, 2) }; const double *Q = P + 3 * i;
            double r[2][3];
            for (int k = 0; k < 2; k++) {
                const double *X = k ? Q : U;
                double v = ee == 0.0 ? 0.5 : (ey * (X[1] - A[1]) + ex * (X[0] - A[0]) + ez * (X[2] - A[2])) / ee;
                if (ee != 0.0) v = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
                if (same) memcpy(r[k], A, sizeof r[k]);
                else { double w = 1.0 - v; r[k][0] = A[0] * w + B[0] * v; r[k][1] = A[1] * w + B[1] * v; r[k][2] = A[2] * w + B[2] * v; }
            }
            double zu = U[2] < r[0][2] ? r[0][2] : U[2], zw = Q[2] < r[1][2] ? r[1][2] : Q[2];
            lu += sqrt((pu[1] - U[1]) * (pu[1] - U[1]) + (pu[0] - U[0]) * (pu[0] - U[0]) + (pu[2] - zu) * (pu[2] - zu));
            lw += sqrt((pw[1] - Q[1]) * (pw[1] - Q[1]) + (pw[0] - Q[0]) * (pw[0] - Q[0]) + (pw[2] - zw) * (pw[2] - zw));
            pu[0] = U[0]; pu[1] = U[1]; pu[2] = zu; pw[0] = Q[0]; pw[1] = Q[1]; pw[2] = zw;
        }
        if (s->dbg) fprintf(stderr, "sd wcheck dist %.17g %.17g cos %.17g %.17g len %.17g %.17g\n", du, dw, cu, cw, lu, lw);
        if (lu < lw) {
            undo = 1;
            for (int i = 0; i < n; i++) if (s->w[4 * i] != 0.0) {
                for (int k = 0; k < 3; k++) s->p[3 * i + k] -= s->w[4 * i + 1 + k];
                s->w[4 * i] = 0.0; s->w[4 * i + 1] = s->w[4 * i + 2] = s->w[4 * i + 3] = 0.0;
            }
        }
    } else if (s->dbg) fprintf(stderr, "sd wcheck dist %.17g %.17g cos %.17g %.17g kept\n", du, dw, cu, cw);
    #undef WU
    return undo;
}
/* a mode-1 connect a -> b (FUN_1801d2bd0 on a connect made with mode 1, FUN_180177a90(.., 1)): FUN_1801cf230 = FUN_1801ce270 on the nodes,
   FUN_1801cd970's XY midpoint at the mean Z, dropped (c3_sd_split mode 1); its path in s (debug lines "sx"); s->rise = the lifted flag +0xc8,
   s->maxrise = +0x38 */
static void c3_sd_line1(C3StayDown *sd, C3Sd *s, const double *a, const double *b) {
    memset(s, 0, sizeof *s); s->mode = 1; s->sd = sd; s->dbg = getenv("MCAM_SD_DEBUG") ? 2 : 0;
    c3_sd_add(s, 0.0f, a); c3_sd_split(s, 0.0f, a, 1.0f, b, 0);
}
/* FUN_18019a310 (hk l.6655-6830, dis 18019a310-18019a69f): the pass extension at the pass point P along the unit direction T. From *ext (first
   SurfaceRampMinPassExtension) in steps of `step` up to `max` (SurfaceRampMaxPassExtension; the step past it is clamped to it and tried once more):
   Q = P + ext T (computed as ext T + P); a mode-1 connect (FUN_1801d1c20 with the mode struct 1 and the base Z +0xa8 = min(P.z, Q.z): dis
   18019a507-18019a535) drops Q's XY from that base Z (FUN_1801cf7b0 at 18019a59a); when the drop is not above Q.z, FUN_1801d2bd0 runs the connect
   P -> Q and the extension is taken when no node was lifted (+0xc8 == 0, 18019a5fb). *ext advances after every try, also after the taken one (as the
   DLL leaves it). 1 = taken, Q in q. */
static int c3_sd_ext(C3StayDown *sd, const double *P, const double *T, double *ext, double step, double max, double *q) {
    int last = 0, ok = 0, dbg = getenv("MCAM_SD_DEBUG") != NULL;
    for (;;) {
        double e = *ext;
        q[0] = e * T[0] + P[0]; q[1] = e * T[1] + P[1]; q[2] = e * T[2] + P[2];
        double base = P[2] < q[2] ? P[2] : q[2], D[3] = { q[0], q[1], base };
        sd->ncf7b0x++; sd->cn->ndrop--;
        c3_lc_drop(sd->cn, D);
        if (dbg) fprintf(stderr, "sx ext (%.17g %.17g %.17g) z0 %.17g -> %.17g\n", q[0], q[1], q[2], base, D[2]);
        if (!(D[2] > q[2])) { C3Sd s; c3_sd_line1(sd, &s, P, q); ok = !s.rise; free(s.t); free(s.p); free(s.rm); }
        if (last) break;
        *ext = step + e;
        if (max < step + e) { *ext = max; last = 1; }
        if (ok) return 1;
    }
    return ok;
}
/* the stay-down link from sd->end to xyz[0] (the next pass xyz[0..m-1]) when it is decoded; 1 = emitted, 2 = the retract FUN_1801a1d70 (the caller's
   c3_link), 0 = not decoded (SAFE) */
static int c3_staydown(C3StayDown *sd, MB *b, const double *xyz, int m, int key, double feed) {
    if (m < 2 || !sd->have) return 0;
    const double *P0 = sd->end, *P1 = xyz;
    if (!(hypot(P1[0] - P0[0], P1[1] - P0[1]) < sd->maxstay)) return 0;       /* LinkMaxStayOnSurface (hk l.33551): else a retract */
    double T1[3] = { xyz[3] - xyz[0], xyz[4] - xyz[1], xyz[5] - xyz[2] }, n1 = sqrt(T1[0] * T1[0] + T1[1] * T1[1] + T1[2] * T1[2]);
    if (!(n1 > 0)) return 0;
    double c[4][3];
    for (int i = 0; i < 3; i++) { c[0][i] = P0[i]; c[1][i] = P0[i] + sd->dir[i] * sd->tf; c[2][i] = P1[i] - T1[i] / n1 * sd->tf; c[3][i] = P1[i]; }
    int mode = key == sd->key ? 1 : 3;                                        /* FUN_1801c3dd0: the same raster line stays kind 1 */
    C3Sd s = { c, NULL, NULL, 0, 0, 0, getenv("MCAM_SD_DEBUG") != NULL, mode, 0.0, sd, NULL, 0, NULL, 0, 0, 0.0, 0.0, NULL, 0 };
    if (s.dbg) fprintf(stderr, "sd link %d (%.17g %.17g %.17g) (%.17g %.17g %.17g)\n", mode, P0[0], P0[1], P0[2], P1[0], P1[1], P1[2]);
    c3_sd_add(&s, 0.0f, P0);
    c3_sd_split(&s, 0.0f, P0, 1.0f, P1, 0);
    int ok = !(s.maxrise > sd->maxvert) && !(mode == 1 && !c3_sd_hwander(&s)); /* a kind-1 path: the wander (FUN_1801dde40); else below */
    if (ok) for (int k = 1; k < s.n; k++) mv(b, 0, s.p[3 * k], s.p[3 * k + 1], s.p[3 * k + 2], feed);
    free(s.t); free(s.p); free(s.rm);
    if (ok) { sd->nstay++; return 1; }
    if (!(s.maxrise > sd->maxvert)) { sd->nsafe++; sd->nkind1++; return 0; }
    /* The rise above SurfaceRampMaxVerticalChange (FUN_180199220 hk l.6203-6560). Raster's passes carry no frag +0x78 and the connect mode's +8 / +0x50
       are empty (traced: the extension runs on every such link of the synthetic cases, formats/hst_linker.md 22), so the pass-extension loop runs: the
       previous pass extended along its end tangent T0 (FUN_18027ad40 at the end, x 1 / |T|), then the next pass backwards along -T1 (its start tangent,
       x 1 / |T| x -1); SurfaceRampMinPassExtension 0x1ef0 = 0.1 x SurfaceRampMaxPassExtension 0x1ef1 = 0.5 x ToolCutterDiameter (defaults hl
       l.13326-13345, not written by mckernel). When both are taken the loop below connects the extension ends anew and emits the extensions
       (FUN_1801a54f0 tag 0x13d4), repeating with longer extensions while that connect rises. When one is not taken on the first round (LAB_180199e80 ->
       LAB_180199eaa): a connect of mode != 1 is retried as a mode-1 connect between the pass ends (FUN_1801d1e70 with FUN_180177a90(.., 1), hk
       l.6510-6535); then (hk l.6537-6560, FUN_1803a4400 first) the connect is emitted when its +0x38 is not above SurfaceRampMaxVerticalChange, else
       the retract FUN_1801a1d70 with a kind-2 link struct, flag 1 (= FUN_180198ef0's retract between passes, hk l.5832-5838: the caller's c3_link). A
       retry that splits (its node path; the wander question of kind 1's FUN_1801dde40) keeps SAFE. */
    double mx = 0.5 * sd->dia, mn = 0.1 * mx, e0 = mn, e1 = mn, q0[3], q1[3], ta[3], tb[3];
    double n0 = sqrt(sd->dir0[0] * sd->dir0[0] + sd->dir0[1] * sd->dir0[1] + sd->dir0[2] * sd->dir0[2]), r0 = n0 == 0.0 ? 1.0 : 1.0 / n0, r1 = 1.0 / n1;
    for (int i = 0; i < 3; i++) { ta[i] = r0 * sd->dir0[i]; tb[i] = r1 * T1[i] * -1.0; }
    int ext = c3_sd_ext(sd, P0, ta, &e0, mn, mx, q0) && c3_sd_ext(sd, P1, tb, &e1, mn, mx, q1);
    if (s.dbg) fprintf(stderr, "sx rise %.17g ext %d\n", s.maxrise, ext);
    if (ext) {
        /* Both extensions taken (hk l.6283-6497): the loop. A side whose extension reached SurfaceRampMaxPassExtension (the ext value == the maximum
           after its try: local_res8 / bVar3) is not extended again. Each round builds the two extension frags - A from the previous pass's end to Q_A,
           B from Q_B to the next pass's start (FUN_180118ee0 + FUN_180121820 / FUN_180121900, emitted by FUN_1801a54f0 tag 0x13d4) - puts them into a
           copy of the connect mode (FUN_180177b90; its +0x20 / +0x28 = local_4c8 / local_4c0) and connects Q_A -> Q_B with a new connect of that mode
           (FUN_1801d1e70 at hk l.6458-6470, the same arguments as FUN_1801a8fe0's; FUN_1801d2bd0 at 180199e23): in mode 3 its curve (FUN_1801d2110 x3
           l.3100-3175) runs from frag A's end along its end tangent to frag B's start along its start tangent (FUN_18027ad40 of the 2-point frags,
           x 1 / |v|), length SurfaceRampTangentFactor. The loop ends when that connect does not rise above SurfaceRampMaxVerticalChange (bVar23), or
           when both sides are at the maximum (LAB_180199e80 with bVar23 as it is), or when a further extension is not taken (LAB_180199e80, bVar23
           false). bVar23 -> (hk l.6537-6607) the extension A, the connect's nodes and the extension B at the feed (FUN_1801a56d0 of local_c80, the
           connect frag, local_c88); else LAB_180199eaa below with the last connect (mode 1: no retry, the retract). */
        int atA = 0, atB = 0, good = 0;
        C3Sd lp = { 0 };
        for (;;) {
            if (e0 == mx) atA = 1;
            if (e1 == mx) atB = 1;
            double va[3] = { q0[0] - P0[0], q0[1] - P0[1], q0[2] - P0[2] }, vb[3] = { P1[0] - q1[0], P1[1] - q1[1], P1[2] - q1[2] };
            double na = sqrt(va[0] * va[0] + va[1] * va[1] + va[2] * va[2]), nb = sqrt(vb[0] * vb[0] + vb[1] * vb[1] + vb[2] * vb[2]);
            na = 1.0 / (na == 0.0 ? 1.0 : na); nb = 1.0 / (nb == 0.0 ? 1.0 : nb);
            double c2[4][3];
            for (int i = 0; i < 3; i++) { c2[0][i] = q0[i]; c2[1][i] = na * va[i] * sd->tf + q0[i]; c2[2][i] = q1[i] - nb * vb[i] * sd->tf; c2[3][i] = q1[i]; }
            free(lp.t); free(lp.p); free(lp.rm);
            C3Sd l0 = { c2, NULL, NULL, 0, 0, 0, getenv("MCAM_SD_DEBUG") ? 2 : 0, mode, 0.0, sd }; lp = l0;
            if (lp.dbg) fprintf(stderr, "sx loop (%.17g %.17g %.17g) (%.17g %.17g %.17g)\n", q0[0], q0[1], q0[2], q1[0], q1[1], q1[2]);
            c3_sd_add(&lp, 0.0f, q0);
            c3_sd_split(&lp, 0.0f, q0, 1.0f, q1, 0);
            lp.c = NULL;
            good = !(lp.maxrise > sd->maxvert);
            if (lp.dbg) fprintf(stderr, "sx loop rise %.17g ext %.17g %.17g\n", lp.maxrise, e0, e1);
            if (good || (atA && atB)) break;
            if (!atA && !c3_sd_ext(sd, P0, ta, &e0, mn, mx, q0)) break;
            if (!atB && !c3_sd_ext(sd, P1, tb, &e1, mn, mx, q1)) break;
        }
        int emit = good && !(mode == 1 && !c3_sd_hwander(&lp));                /* a kind-1 connect: the wander (FUN_1801dde40) */
        if (emit) {
            mv(b, 0, q0[0], q0[1], q0[2], feed);
            for (int k = 1; k < lp.n; k++) mv(b, 0, lp.p[3 * k], lp.p[3 * k + 1], lp.p[3 * k + 2], feed);
            mv(b, 0, P1[0], P1[1], P1[2], feed);
        }
        free(lp.t); free(lp.p); free(lp.rm);
        if (emit) { sd->nstay++; sd->next++; return 1; }
        if (good) { sd->nsafe++; sd->nkind1++; return 0; }
        if (mode == 1) { sd->nret++; return 2; }
    } else if (mode == 1) { sd->nret++; return 2; }
    C3Sd r; c3_sd_line1(sd, &r, P0, P1);
    int ret = r.maxrise > sd->maxvert, emit = !ret && c3_sd_hwander(&r);      /* the retry's path after the wander (FUN_1801dde40) */
    if (emit) for (int k = 1; k < r.n; k++) mv(b, 0, r.p[3 * k], r.p[3 * k + 1], r.p[3 * k + 2], feed);
    free(r.t); free(r.p); free(r.rm);
    if (emit) { sd->nstay++; sd->nretry++; return 1; }
    if (ret) { sd->nret++; return 2; }
    sd->nsafe++; sd->nkind1++; return 0;
}
/* FUN_1801990c0 (hk l.5868-5915, dis 1801990c0-18019920e) with LinkRetractStyle 2 (its 4th argument, plan +0x1109): the mode-1 connect A -> B
   (FUN_1801d1c20 + FUN_1801d2bd0, layers 0, the wander flag set: traced, the new nodes start at the mean Z of their neighbours) - FUN_1801cf230 =
   FUN_1801ce270 on the nodes, FUN_1801cd970's XY midpoint dropped (c3_lc_drop), split when farther than StepMachTolerance from the chord - and
   its highest node z (+0x30, FUN_180006910 x3 l.40-125); h = LinkZClearance when that is not below it, else the highest node + 1.0 (mm), and at
   least max(A.z, B.z). Checked call by call against mchmm.dll (HMM_TRACE inside FUN_1801990c0, formats/hst_linker.md 21). */
static void c3_lk_mid1(C3Link *lk, const double *a, const double *b, double *zmax, int depth) {
    lk->npz_ce++;
    int split = c3_lc_chord(lk, a, b); lk->ncc--;
    int dbg = getenv("MCAM_LK_DEBUG") != NULL;
    if (dbg) fprintf(stderr, "rt ce270 (%.17g %.17g %.17g) (%.17g %.17g %.17g) -> %d\n", a[0], a[1], a[2], b[0], b[1], b[2], split);
    double gx = a[0] - b[0], gy = a[1] - b[1], g2 = gx * gx + gy * gy;
    if (!split && !(g2 > lk->hmin * lk->hmin)) return;
    double M[3] = { a[0] * 0.5 + b[0] * 0.5, a[1] * 0.5 + b[1] * 0.5, a[2] == b[2] ? a[2] : b[2] * 0.5 + a[2] * 0.5 };
    double z0 = M[2]; lk->npz_drop++; lk->ndrop--;
    int up = c3_lc_drop(lk, M);
    if (dbg) fprintf(stderr, "rt cf7b0 (%.17g %.17g %.17g) z0 %.17g -> %d\n", M[0], M[1], M[2], z0, up);
    if (!split && g2 > lk->hmin * lk->hmin) {
        double ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2], ee = ex * ex + ey * ey + ez * ez;
        double v = ee == 0.0 ? 0.0 : ((M[0] - a[0]) * ex + (M[1] - a[1]) * ey + (M[2] - a[2]) * ez) / ee; v = v < 0 ? 0 : v > 1 ? 1 : v;
        double fx = M[0] - (a[0] + v * ex), fy = M[1] - (a[1] + v * ey), fz = M[2] - (a[2] + v * ez);
        split = sqrt(fx * fx + fy * fy + fz * fz) > lk->smt;
    }
    if (!split) return;
    if (M[2] > *zmax) *zmax = M[2];                                           /* FUN_180007eb0: the highest inserted node */
    if (depth > 200) return;                                                  /* (a stack bound, not reached) */
    c3_lk_mid1(lk, a, M, zmax, depth + 1);
    c3_lk_mid1(lk, M, b, zmax, depth + 1);
}
static double c3_lk_pathz(C3Link *lk, const double *A, const double *B) {
    if (A[0] == B[0] && A[1] == B[1] && A[2] == B[2]) return A[2];           /* FUN_18002d030: the same point */
    double hz = fmax(A[2], B[2]);
    c3_lk_mid1(lk, A, B, &hz, 0);
    double h = lk->clr < hz ? hz + 1.0 : lk->clr;
    return fmax(h, fmax(A[2], B[2]));
}
/* The exit's FUN_1801a44a0 (param_5 = 0) on the previous pass, already emitted at the end of b (its points from the second on, c3_emit_run): the
   moves past the 3D length L3 - v come off and the trim point P goes on. 0 = b's tail is not that pass (the caller keeps SAFE, nothing changed) */
static int c3_lk_cut_prev(C3Link *lk, MB *b, double v, int doit) {
    const double *pp = lk->pp; int m = lk->ppn, k; double L3 = hl_len3(pp, m), P[3]; int seg;
    hl_at3(pp, m, L3 - fabs(v), P, &seg);
    int pop = m - 1 - seg;                                                    /* the vertices after P's segment start: seg + 1 .. m - 1 */
    if (pop < 1 || b->n < pop) return 0;
    for (k = 0; k < pop; k++) { V3 l = b->m[b->n - 1 - k].p; const double *e = pp + 3 * (m - 1 - k); if (l.x != e[0] || l.y != e[1] || l.z != e[2] || b->m[b->n - 1 - k].rapid) return 0; }
    if (!doit) return 1;
    b->n -= pop;
    mv(b, 0, P[0], P[1], P[2], b->m[b->n].feed);
    return 1;
}
/* the side's lead after a fitting trim (hl_rise *s): exit [Q, C', T'] / approach [T', C', Q] (FUN_1801d8ce0), or Q alone (FUN_1801d7b00) */
static const double *c3_lk_tq(const HlSide *s, int ex) { return ex ? s->o.p6 : s->o.p8; }
static const double *c3_lk_tt(const HlSide *s, int ex) { return s->one ? c3_lk_tq(s, ex) : ex ? s->o.p8 : s->o.p6; }
/* the exit with a fitting trim: the previous pass cut at P, FUN_1801a47b0's arcs P -> Q, the fitted arc Q -> T' (the connect's nodes) */
static int c3_lk_trim_out(C3Link *lk, MB *b, const HlSide *s, double R, double feed) {
    if (!c3_lk_cut_prev(lk, b, s->o.v, 1)) return 0;
    if (!hl_trim_arcs(lk, b, lk->pp, lk->ppn, 1, R, s->o.v, s->o.c0, s->a0, 0.0, feed)) return 0;
    if (!s->one) hl_arc_to(lk, b, s->o.p7, c3_lk_tt(s, 1), c3_lc_r(s->o.p7, c3_lk_tt(s, 1)), feed);
    lk->ntrim++;
    return 1;
}
/* the approach with a fitting trim, from T' (reached): the fitted arc T' -> Q, FUN_1801a47b0's arcs Q -> P; the pass then starts at P (lk->tin) */
static int c3_lk_trim_in(C3Link *lk, MB *b, const HlSide *s, const double *xyz, int m, double R, double feed) {
    const double *Q = c3_lk_tq(s, 0);
    if (!s->one) hl_arc_to(lk, b, s->o.p7, Q, c3_lc_r(s->o.p7, Q), feed);
    if (!hl_trim_arcs(lk, b, xyz, m, 0, R, s->o.v, s->o.c0, s->a0, Q[2], feed)) return 0;
    lk->tin = 1; lk->tinv = fabs(s->o.v); lk->ntrim++;
    return 1;
}
/* LinkTrimmingStyle 2 (record 0x16d1 = 2, plan +0x110a / +0x110b both 0): a lead connect called with param_4 = 1 skips the lead nodes -
   FUN_1801db570 (x7 l.1-120; the exit's FUN_1801daf90 the mirror) leaves FUN_1801db820 out and runs FUN_1801d7b00 with the lead radius (the plan's
   +0x1128 LinkVerticalLeadInRadius / +0x1130 Out; 0 when the frag's length +0x1d8 <= 0, never on a pass): FUN_180171b50 with overlap 0 on
   FUN_1801dbf80's pass nodes (hl_rise's second half); v != 0 leaves the one node Q (x8 l.226-290), v = 0 no lead node at all. Who calls with
   param_4 = 1: the retract's first FUN_1801d69f0 call for both sides (hk l.11935-11949: (style == 2), the exit only with param_6), its second call
   for a side whose trim fits (hk l.12868-12880), the first approach's first call (hk l.3112-3118; Raster is kind 1) and the final exit's first
   call (hk l.5478-5485); the second calls of those two pass 0 (+0x110b is 0: hk l.3155-3162 / 5519-5526) with the attempts off. Traced on e4
   (c45 with style 2, LinkMaxAllowedTrimming 0.25 in): 70 FUN_1801d7b00, 70 FUN_180171b50, no FUN_1801d8240, 64 trimmings.
   One side's skip call: 3 = a trim that fits (*s: one node), 4 = no lead (v = 0), 5 = the trim does not fit (the second call: the lead nodes,
   attempts off), 0 = NOT DECODED */
static int c3_lk_s2(C3Link *lk, const double *xyz, int m, int ex, double R, HlSide *sd) {
    memset(sd, 0, sizeof *sd); sd->one = 1;
    if (m < 2) return 0;
    double v = 0.0;
    if (R != 0.0) {
        double *q = malloc(sizeof(double) * 3 * (size_t)(m + 1));
        int n = hl_pnodes(xyz, m, ex, (double)2.5f * R, q), ok = hl_fit(q, n, R, ex, 0.0, &sd->o);
        free(q);
        if (!ok) return 0;
        lk->nfit++;
        if (getenv("MCAM_LK_DEBUG")) fprintf(stderr, "la trim v %.17g c0 %.17g cnt %d (style 2 %s)\n", sd->o.v, sd->o.c0, sd->o.cnt, ex ? "out" : "in");
        if (sd->o.v != 0.0 && sd->o.cnt != 0) return 0;                       /* a closed pass walked round: NOT DECODED */
        v = sd->o.v;
    }
    if (!(fabs(v) <= lk->maxtrim && fabs(v) < c3_lk_xylen(xyz, m))) return 5;
    return v != 0.0 ? 3 : 4;
}
/* the approach of the pass xyz (lead-in) and, when ex is set, the exit of the previous pass (lead-out) with the retract between them; 1 = emitted */
static int c3_link(C3Link *lk, MB *b, const double *xyz, int m, int ex, double feed, double plunge) {
    if (m < 2) return 0;
    double Ti[3] = { xyz[3] - xyz[0], xyz[4] - xyz[1], xyz[5] - xyz[2] }, ni = sqrt(Ti[0] * Ti[0] + Ti[1] * Ti[1] + Ti[2] * Ti[2]);
    if (!(ni > 0)) return 0;
    for (int i = 0; i < 3; i++) Ti[i] /= ni;
    C3Lead Li, Lo; double ti[3], to[3], h = lk->clr; int vin = 0, vout = 0, tro = 0, tri = 0, nlo = 0, nli = 0; HlSide so, si;
    if (lk->rin > 0 && !c3_lk_lead(xyz, Ti, lk->rin, 1, &Li)) return 0;
    if (lk->tstyle == 2) {                                                    /* LinkTrimmingStyle 2: the skip calls (c3_lk_s2) */
        if (ex && (!lk->have || (lk->rout > 0 && !c3_lk_lead(lk->end, lk->dir, lk->rout, 0, &Lo)))) return 0;
        int mo = ex ? c3_lk_s2(lk, lk->pp, lk->ppn, 1, lk->rout, &so) : 4, mi = c3_lk_s2(lk, xyz, m, 0, lk->rin, &si);
        if (!mo || !mi) { lk->nblock++; return 0; }
        if (mo == 5 || mi == 5) {                                             /* the second call: a fitting side skips again, the other gets its lead */
            if (ex && mo != 5) mo = c3_lk_s2(lk, lk->pp, lk->ppn, 1, lk->rout, &so);
            if (!ex) ;
            else if (mo == 5 && lk->rout > 0) { int r = c3_lc_lead_r(lk, lk->end, &Lo, 0); vout = r == 2; memcpy(to, Lo.top, sizeof to); }
            else if (mo == 5) memcpy(to, lk->end, sizeof to);
            if (mi != 5) mi = c3_lk_s2(lk, xyz, m, 0, lk->rin, &si);
            else if (lk->rin > 0) { int r = c3_lc_lead_r(lk, xyz, &Li, 1); vin = r == 2; memcpy(ti, Li.top, sizeof ti); }
            else memcpy(ti, xyz, sizeof ti);
            if (!mo || !mi) { lk->nblock++; return 0; }
        }
        if (ex) {
            if (mo == 3) { if (fabs(so.o.c0) > fabs(so.o.v) || !c3_lk_cut_prev(lk, b, so.o.v, 0)) { lk->nblock++; return 0; } tro = 1; memcpy(to, c3_lk_tt(&so, 1), sizeof to); }
            else if (mo == 4) { nlo = 1; memcpy(to, lk->end, sizeof to); }
            if (vout) { memcpy(to, lk->end, sizeof to); to[2] += c3_lc_r(Lo.C, Lo.top); lk->nvert++; }   /* FUN_1801d8630: E.z + R */
        }
        if (mi == 3) { if (fabs(si.o.c0) > fabs(si.o.v)) { lk->nblock++; return 0; } tri = 1; memcpy(ti, c3_lk_tt(&si, 0), sizeof ti); }
        else if (mi == 4) { nli = 1; memcpy(ti, xyz, sizeof ti); }
        if (vin) { memcpy(ti, xyz, sizeof ti); ti[2] += c3_lc_r(Li.C, Li.top); lk->nvert++; }
    } else if (ex) {
        /* the retract between passes (FUN_1801a1d70 -> FUN_1801d69f0 x1 l.112-215 for Raster: FUN_1801a1240 runs only with a horizontal lead radius
           +0x11f8 / +0x11f0 > 0, hk l.11981-12051): the exit lead FUN_1801dab60 at the previous pass's end, then the approach lead FUN_1801db250 at the
           next pass's start - the lead connects of the final exit / first approach, with their collision handling (c3_lc_lead; a rise keeps SAFE,
           N13-29) - then FUN_1801d62e0 and FUN_1801d7210 (x1 l.375-467): the height h of FUN_1801990c0 (c3_lk_pathz) between the two lead points */
        if (!lk->have) return 0;
        int ro = 1, ri = 1;
        if (lk->rout > 0) {
            if (!c3_lk_lead(lk->end, lk->dir, lk->rout, 0, &Lo)) return 0;
            ro = c3_lc_lead_r(lk, lk->end, &Lo, 0);
            memcpy(to, Lo.top, sizeof to);
        } else memcpy(to, lk->end, sizeof to);
        if (lk->rin > 0) {
            ri = c3_lc_lead_r(lk, xyz, &Li, 1);
            memcpy(ti, Li.top, sizeof ti);
        } else memcpy(ti, xyz, sizeof ti);
        if (!ro || !ri) { lk->nblock++; return 0; }                           /* an arc refinement that rises: NOT DECODED (SAFE, N13-29) */
        if (ro == 2 || ri == 2) {                                             /* a top that rises: the attempts (cam3d_hstlink.c) */
            int r = c3_lk_attempts(lk, xyz, m, ro == 2 ? &Lo : NULL, ri == 2 ? &Li : NULL, &vout, &vin, &so, &si, &tro, &tri);
            if (r <= 0) { lk->nblock++; return 0; }
            if (tro && (fabs(so.o.c0) > fabs(so.o.v) || !c3_lk_cut_prev(lk, b, so.o.v, 0))) { lk->nblock++; return 0; }
            if (tri && fabs(si.o.c0) > fabs(si.o.v)) { lk->nblock++; return 0; }
            if (vout) { memcpy(to, lk->end, sizeof to); to[2] += c3_lc_r(Lo.C, Lo.top); lk->nvert++; }   /* FUN_1801d8630: E.z + R */
            if (vin) { memcpy(ti, xyz, sizeof ti); ti[2] += c3_lc_r(Li.C, Li.top); lk->nvert++; }
            if (tro) memcpy(to, c3_lk_tt(&so, 1), sizeof to);
            if (tri) memcpy(ti, c3_lk_tt(&si, 0), sizeof ti);
        }
    } else if (lk->rin > 0) {
        /* the first approach (FUN_1801db250): through the decoded collision handling (c3_lc_lead); a top that rises: c3_lk_one */
        int r = c3_lc_lead_r(lk, xyz, &Li, 1);
        if (!r) { lk->nblock++; return 0; }
        if (r == 2) {
            r = c3_lk_one(lk, xyz, m, &Li, 0, &si);
            if (!r || (r == 3 && fabs(si.o.c0) > fabs(si.o.v))) { lk->nblock++; return 0; }
            if (r == 3) { tri = 1; memcpy(ti, c3_lk_tt(&si, 0), sizeof ti); }
            else { vin = 1; memcpy(ti, xyz, sizeof ti); ti[2] += c3_lc_r(Li.C, Li.top); lk->nvert++; }   /* FUN_1801d8630: S.z + R */
        } else memcpy(ti, Li.top, sizeof ti);
    } else memcpy(ti, xyz, sizeof ti);
    if (ex) {
        double za = to[2] + lk->off, zb = ti[2] + lk->off;
        double A[3] = { to[0], to[1], za }, B[3] = { ti[0], ti[1], zb };
        h = c3_lk_pathz(lk, A, B);
        if (tro) { if (!c3_lk_trim_out(lk, b, &so, lk->rout, feed)) return 0; }   /* (checked above: not reached) */
        else if (lk->rout > 0 && !vout && !nlo) {                                /* the lead-out arc: from E (radius dir sin b - z cos b) to the top (radius dir) */
            double u[3] = { Lo.dir[0] * sin(Lo.a), Lo.dir[1] * sin(Lo.a), -cos(Lo.a) }, v[3] = { Lo.dir[0] * cos(Lo.a), Lo.dir[1] * cos(Lo.a), sin(Lo.a) };
            c3_lk_arc(lk, b, Lo.C, u, v, lk->rout, M_PI / 2 - Lo.a, to, feed);
        }
        if (vout) mv(b, 0, to[0], to[1], to[2], feed);                          /* the vertical lead (FUN_1801d8630): straight up from E */
        mv(b, 0, to[0], to[1], za, feed);
        mv(b, 1, to[0], to[1], h, 0);
    }
    mv(b, 1, ti[0], ti[1], h, 0);
    mv(b, 1, ti[0], ti[1], ti[2] + lk->off, 0);
    mv(b, 0, ti[0], ti[1], ti[2], plunge);
    if (tri) c3_lk_trim_in(lk, b, &si, xyz, m, lk->rin, plunge);              /* the fitted arc, FUN_1801a47b0's arcs; the pass from P */
    else if (vin) mv(b, 0, xyz[0], xyz[1], xyz[2], plunge);                      /* the vertical lead (FUN_1801d8630): straight down to S */
    else if (lk->rin > 0 && !nli) {                                               /* the lead-in arc: from the top (radius -dir) to S (radius -(dir sin a + z cos a)) */
        double u[3] = { -Li.dir[0], -Li.dir[1], 0 }, v[3] = { 0, 0, -1 };
        c3_lk_arc(lk, b, Li.C, u, v, lk->rin, M_PI / 2 - Li.a, xyz, plunge);
    }
    return 1;
}
/* the final exit after the last pass (lead-out, offset, rapid to LinkZClearance); 1 = emitted */
static int c3_link_end(C3Link *lk, MB *b, double feed) {
    if (!lk->have) return 0;
    C3Lead Lo; double to[3]; int vout = 0, tro = 0, nlo = 0; HlSide so;
    if (lk->tstyle == 2) {                                                    /* the skip call (c3_lk_s2), its second call without the attempts */
        if (lk->rout > 0 && !c3_lk_lead(lk->end, lk->dir, lk->rout, 0, &Lo)) return 0;
        int mo = c3_lk_s2(lk, lk->pp, lk->ppn, 1, lk->rout, &so);
        if (!mo || (mo == 3 && (fabs(so.o.c0) > fabs(so.o.v) || !c3_lk_cut_prev(lk, b, so.o.v, 0)))) { lk->nblock++; return 0; }
        if (mo == 3) { tro = 1; memcpy(to, c3_lk_tt(&so, 1), sizeof to); }
        else if (mo == 4 || lk->rout <= 0) { nlo = 1; memcpy(to, lk->end, sizeof to); }
        else if (c3_lc_lead_r(lk, lk->end, &Lo, 0) == 2) { vout = 1; memcpy(to, lk->end, sizeof to); to[2] += c3_lc_r(Lo.C, Lo.top); lk->nvert++; }
        else memcpy(to, Lo.top, sizeof to);
    } else if (lk->rout > 0) {
        if (!c3_lk_lead(lk->end, lk->dir, lk->rout, 0, &Lo)) return 0;
        int r = c3_lc_lead_r(lk, lk->end, &Lo, 0);                            /* FUN_1801dab60's collision handling (c3_lc_lead) */
        if (!r) { lk->nblock++; return 0; }
        if (r == 2) {                                                         /* the top rises: the attempts, else the vertical lead (c3_lk_one) */
            r = c3_lk_one(lk, lk->pp, lk->ppn, &Lo, 1, &so);
            if (!r || (r == 3 && (fabs(so.o.c0) > fabs(so.o.v) || !c3_lk_cut_prev(lk, b, so.o.v, 0)))) { lk->nblock++; return 0; }
            if (r == 3) { tro = 1; memcpy(to, c3_lk_tt(&so, 1), sizeof to); }
            else { vout = 1; memcpy(to, lk->end, sizeof to); to[2] += c3_lc_r(Lo.C, Lo.top); lk->nvert++; }   /* FUN_1801d8630: E.z + R */
        } else memcpy(to, Lo.top, sizeof to);
    } else memcpy(to, lk->end, sizeof to);
    if (!(to[2] + lk->off < lk->clr)) return 0;
    if (tro) { if (!c3_lk_trim_out(lk, b, &so, lk->rout, feed)) return 0; }   /* FUN_180197b80 hk l.5546-5559: the trimmed pass, FUN_1801a47b0, the connect */
    else if (vout) mv(b, 0, to[0], to[1], to[2], feed);                       /* the vertical lead: straight up from E */
    else if (lk->rout > 0 && !nlo) {
        double u[3] = { Lo.dir[0] * sin(Lo.a), Lo.dir[1] * sin(Lo.a), -cos(Lo.a) }, v[3] = { Lo.dir[0] * cos(Lo.a), Lo.dir[1] * cos(Lo.a), sin(Lo.a) };
        c3_lk_arc(lk, b, Lo.C, u, v, lk->rout, M_PI / 2 - Lo.a, to, feed);
    }
    mv(b, 0, to[0], to[1], to[2] + lk->off, feed);
    mv(b, 1, to[0], to[1], lk->clr, 0);
    return 1;
}
/* one pass as the finishing styles emit it: the link (the stay-down link above when e->sd is set and it applies; else the retract link above when
   e->lk is set and it applies; else the SAFE link: up to the feed plane, across, plunge - ours, as before), then the pass at the feed */
typedef struct { MB *b; double fp, plunge, feed; int *down; C3StayDown *sd; C3Link *lk; int key; } C3Emit;
static void c3_emit_run(const double *xyz0, int m, void *ctx) {
    C3Emit *e = ctx; MB *b = e->b; const double *xyz = xyz0; double *xt = NULL;
    int sdr = *e->down && e->sd ? c3_staydown(e->sd, b, xyz, m, e->key, e->feed) : 0, done = sdr == 1;
    if (!done && e->lk) {
        /* a retract (link kind 2) when the XY gap is >= LinkMaxStayOnSurface or a stay-down link's rise ends in FUN_1801a1d70 (c3_staydown 2); else a
           stay-down link that is NOT DECODED (SAFE) */
        int kind2 = !*e->down || !e->lk->have || !(hypot(xyz[0] - e->lk->end[0], xyz[1] - e->lk->end[1]) < e->lk->maxstay) || sdr == 2;
        if (kind2 && c3_link(e->lk, b, xyz, m, *e->down, e->feed, e->plunge)) { done = 1; *e->down = 1; e->lk->nret++; }
        else { e->lk->nsafe++; if (getenv("MCAM_LK_DEBUG")) fprintf(stderr, "lk SAFE link to (%.17g %.17g %.17g) kind2 %d\n", xyz[0], xyz[1], xyz[2], kind2); }
    }
    if (!done) {
        if (*e->down) { V3 l = lastp(b); mv(b, 1, l.x, l.y, e->fp, 0); }
        mv(b, 1, xyz[0], xyz[1], e->fp, 0); mv(b, 0, xyz[0], xyz[1], xyz[2], e->plunge); *e->down = 1;
    }
    if (done && e->lk && e->lk->tin) {                                         /* FUN_1801a44a0 (param_5 = 1): the pass from P, the 3D length tinv in */
        double P[3], s = 0.0; int seg; hl_at3(xyz0, m, e->lk->tinv, P, &seg);
        xt = malloc(sizeof(double) * 3 * (size_t)(m + 1)); memcpy(xt, P, sizeof P); int n = 1;
        for (int q = 1; q < m; q++) {
            s += sqrt((xyz0[3*q] - xyz0[3*q-3]) * (xyz0[3*q] - xyz0[3*q-3]) + (xyz0[3*q+1] - xyz0[3*q-2]) * (xyz0[3*q+1] - xyz0[3*q-2]) + (xyz0[3*q+2] - xyz0[3*q-1]) * (xyz0[3*q+2] - xyz0[3*q-1]));
            if (s > e->lk->tinv) { memcpy(xt + 3 * n, xyz0 + 3 * q, sizeof(double) * 3); n++; }
        }
        e->lk->tin = 0; xyz = xt; m = n;
        { V3 l = lastp(b); if (l.x != P[0] || l.y != P[1] || l.z != P[2]) mv(b, 0, P[0], P[1], P[2], e->feed); }
    }
    for (int q = 1; q < m; q++) mv(b, 0, xyz[3 * q], xyz[3 * q + 1], xyz[3 * q + 2], e->feed);
    if (e->sd && m >= 2) {                                                      /* the end and its tangent (FUN_18027ad40 on the pass) for the next link */
        const double *pe = xyz + 3 * (m - 1), *pp = xyz + 3 * (m - 2);
        double d[3] = { pe[0] - pp[0], pe[1] - pp[1], pe[2] - pp[2] }, n = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (n > 0) { for (int i = 0; i < 3; i++) { e->sd->end[i] = pe[i]; e->sd->dir[i] = d[i] / n; e->sd->dir0[i] = d[i]; } e->sd->have = 1; e->sd->key = e->key; } else e->sd->have = 0;
    } else if (e->sd) e->sd->have = 0;
    if (e->lk) {                                                                /* the end and its unit 3D tangent (FUN_18027ad40) for the next link / the final exit */
        e->lk->have = 0;
        if (e->lk->ppcap < m) { e->lk->ppcap = m; e->lk->pp = realloc(e->lk->pp, sizeof(double) * 3 * (size_t)m); }   /* the pass (the exit lead's FUN_1801dbf80) */
        memcpy(e->lk->pp, xyz, sizeof(double) * 3 * (size_t)m); e->lk->ppn = m;
        if (m >= 2) {
            const double *pe = xyz + 3 * (m - 1), *pp = xyz + 3 * (m - 2);
            double d[3] = { pe[0] - pp[0], pe[1] - pp[1], pe[2] - pp[2] }, n = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (n > 0) { for (int i = 0; i < 3; i++) { e->lk->end[i] = pe[i]; e->lk->dir[i] = d[i] / n; } e->lk->have = 1; }
        }
    }
    free(xt);
}
/* the pass through the check trimming when the op has it (chk), else as it is; the number of passes emitted */
static int c3_chk_emit(const C3Chk *chk, const double *xyz, int m, int closed, C3Emit *e) {
    if (!chk) { c3_emit_run(xyz, m, e); return 1; }
    return c3_chk_trim(chk, xyz, m, closed, c3_emit_run, e);
}
/* even-odd point in the containment */
static int wl_inside(const CPaths *r, double x, double y) {
    int in = 0;
    for (int i = 0; i < r->n; i++) { const CPath *L = &r->p[i];
        for (int a = 0, c = L->n - 1; a < L->n; c = a++) { double ax = L->xy[2 * a], ay = L->xy[2 * a + 1], cx = L->xy[2 * c], cy = L->xy[2 * c + 1];
            if ((ay > y) != (cy > y) && x < ax + (y - ay) * (cx - ax) / (cy - ay)) in = !in; } }
    return in;
}
/* The contact angle filter of a level's loops: FUN_18040b6f0 (hz l.11835-12215), EvaMachLocBasePlan's cosines set in its constructor FUN_1802f2340 (mchmm.dll):
   +0x678 = cos(ContactAngleHi) (-0.1 when Hi > 90 - Tol), +0x680 = cos(ContactAngleLo) (1.1 when Lo < Tol), +0x688 / +0x690 = the same with Hi + Tol / Lo - Tol
   (decompile l.504-546 of that constructor, the Eva +0x670 switch set just before them). A loop vertex is kept when its tool contact normal's Z (FUN_1802538d0) is
   within [+0x678, +0x680]; a run of dropped vertices between two kept ones is put back when no vertex of it leaves the wider window [+0x688, +0x690], or when the path
   across it is shorter than PassesJoinGaps (0x1ad6, default 1.0 mm: FUN_180310790 hp l.725-731). ContactAngleTol (0x1ac6) default: the smaller of
   (Lo > 0 ? min(Lo / 10, 1) : 1) and (Hi < 90 ? min((90 - Hi) / 10, 1) : 1) degrees (the default expression in mchmm.dll FUN_18030a730).
   Not modelled (ours, no tool assembly model): FUN_1801344c0's drop of contacts on the tool's upper layers. Ours: the normal is found on the mesh: the point of the
   nearby triangles closest to the tool's ball-centre circle (radius Fo at height zc), a point above zc lying on the flute (normal horizontal, as the tool profile's
   cylinder layer in FUN_1802538d0). */
typedef struct { const C3Mesh *M; double cell, x0, y0; int nx, ny; int *head, *next, *tri; } WlTg;
static void wltg_build(WlTg *g, const C3Mesh *M, double cell) {
    g->M = M; g->cell = cell; g->x0 = M->lo[0] - cell; g->y0 = M->lo[1] - cell;
    g->nx = (int)((M->hi[0] - M->lo[0]) / cell) + 3; g->ny = (int)((M->hi[1] - M->lo[1]) / cell) + 3;
    g->head = malloc(sizeof(int) * (size_t)g->nx * g->ny); for (int i = 0; i < g->nx * g->ny; i++) g->head[i] = -1;
    int cap = M->n * 4 + 16, used = 0; g->next = malloc(sizeof(int) * cap); g->tri = malloc(sizeof(int) * cap);
    for (int t = 0; t < M->n; t++) {
        const double *v = M->t + 9 * t;
        double xl = fmin(v[0], fmin(v[3], v[6])), xh = fmax(v[0], fmax(v[3], v[6])), yl = fmin(v[1], fmin(v[4], v[7])), yh = fmax(v[1], fmax(v[4], v[7]));
        int i0 = (int)((xl - g->x0) / cell), i1 = (int)((xh - g->x0) / cell), j0 = (int)((yl - g->y0) / cell), j1 = (int)((yh - g->y0) / cell);
        for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
            if (i < 0 || j < 0 || i >= g->nx || j >= g->ny) continue;
            if (used == cap) { cap *= 2; g->next = realloc(g->next, sizeof(int) * cap); g->tri = realloc(g->tri, sizeof(int) * cap); }
            int c = j * g->nx + i; g->tri[used] = t; g->next[used] = g->head[c]; g->head[c] = used++;
        }
    }
}
static void wltg_free(WlTg *g) { free(g->head); free(g->next); free(g->tri); }
/* the point of triangle (a, b, c) closest to q (Ericson, Real-Time Collision Detection) */
static void wl_tri_closest(const double *a, const double *b, const double *c, const double *q, double *r) {
    double ab[3], ac[3], ap[3]; for (int i = 0; i < 3; i++) { ab[i] = b[i] - a[i]; ac[i] = c[i] - a[i]; ap[i] = q[i] - a[i]; }
    #define DT(u, v) (u[0] * v[0] + u[1] * v[1] + u[2] * v[2])
    double d1 = DT(ab, ap), d2 = DT(ac, ap);
    if (d1 <= 0 && d2 <= 0) { memcpy(r, a, 24); return; }
    double bp[3]; for (int i = 0; i < 3; i++) bp[i] = q[i] - b[i];
    double d3 = DT(ab, bp), d4 = DT(ac, bp);
    if (d3 >= 0 && d4 <= d3) { memcpy(r, b, 24); return; }
    double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) { double v = d1 / (d1 - d3); for (int i = 0; i < 3; i++) r[i] = a[i] + v * ab[i]; return; }
    double cp[3]; for (int i = 0; i < 3; i++) cp[i] = q[i] - c[i];
    double d5 = DT(ab, cp), d6 = DT(ac, cp);
    if (d6 >= 0 && d5 <= d6) { memcpy(r, c, 24); return; }
    double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) { double w = d2 / (d2 - d6); for (int i = 0; i < 3; i++) r[i] = a[i] + w * ac[i]; return; }
    double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) { double w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); for (int i = 0; i < 3; i++) r[i] = b[i] + w * (c[i] - b[i]); return; }
    double den = 1.0 / (va + vb + vc), v = vb * den, w = vc * den; for (int i = 0; i < 3; i++) r[i] = a[i] + ab[i] * v + ac[i] * w;
    #undef DT
}
/* the Z of the contact normal of the tool (ball Rb on a circle Fo at height zc) at axis position (x, y); 2.0 = no contact found */
static double wl_contact_nz(const WlTg *g, double x, double y, double zc, double Fo, double Rb) {
    double best = 1e300, bp[3] = { 0, 0, 0 }, bc[3] = { 0, 0, 0 }, reach = Fo + Rb * 1.25 + 1e-9;
    int i0 = (int)floor((x - reach - g->x0) / g->cell), i1 = (int)floor((x + reach - g->x0) / g->cell), j0 = (int)floor((y - reach - g->y0) / g->cell), j1 = (int)floor((y + reach - g->y0) / g->cell);
    for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
        if (i < 0 || j < 0 || i >= g->nx || j >= g->ny) continue;
        for (int e = g->head[j * g->nx + i]; e >= 0; e = g->next[e]) {
            const double *v = g->M->t + 9 * g->tri[e];
            double xl = fmin(v[0], fmin(v[3], v[6])), xh = fmax(v[0], fmax(v[3], v[6])), yl = fmin(v[1], fmin(v[4], v[7])), yh = fmax(v[1], fmax(v[4], v[7]));
            if (xl > x + reach || xh < x - reach || yl > y + reach || yh < y - reach) continue;
            if (fmin(v[2], fmin(v[5], v[8])) > zc + reach || fmax(v[2], fmax(v[5], v[8])) < zc - reach) continue;
            double a = Fo > 1e-9 ? atan2(((v[1] + v[4] + v[7]) / 3) - y, ((v[0] + v[3] + v[6]) / 3) - x) : 0.0, c[3] = { x + Fo * cos(a), y + Fo * sin(a), zc }, p[3];
            wl_tri_closest(v, v + 3, v + 6, c, p);
            if (Fo > 1e-9) for (int it = 0; it < 3; it++) {   /* the circle point nearest the triangle point, and back */
                double ax = atan2(p[1] - y, p[0] - x); c[0] = x + Fo * cos(ax); c[1] = y + Fo * sin(ax);
                wl_tri_closest(v, v + 3, v + 6, c, p);
            }
            double d = hypot(hypot(p[0] - c[0], p[1] - c[1]), p[2] - c[2]);
            if (d < best) { best = d; memcpy(bp, p, 24); memcpy(bc, c, 24); }
        }
    }
    if (best > 1e299 || best < 1e-12) return 2.0;
    if (bp[2] > bc[2]) return 0.0;                                   /* on the flute: the normal is horizontal */
    return (bc[2] - bp[2]) / best;
}
/* The weave contact of a loop vertex (formats/hst_linker.md 31; the words of cam3d_rough.c c3_ct, carried by wv_loops from the drop to the node as McHmm does).
   FUN_1801344c0 (hst3d_weave_drop.c; FUN_18040b6f0 calls it with Eva +0x6e0 / +0x6e4): a contact on a profile layer above the ball is dropped - with +0x6e0 = -1
   never, 0 any layer > 0, else a layer above +0x6e0, or equal to it when +0x6e4 is 0 and the word's flag (bit 24) is set. +0x6e0 = PosContactOnly ? 1 : -1
   (FUN_18040ea00, hst3d_port_plan.md 7), +0x6e4 = 1 (EvaZProfilePlan's constructor FUN_1803f9c20 hz l.349, no other writer). */
static int wl_ct_layer_drop(unsigned w, int e6e0, int e6e4) {
    if (e6e0 == -1) return 0;
    int lay = (int)(w >> 25 & 0x3f);
    if (lay == 0) return 0;
    if (e6e0 == 0 || e6e0 < lay) return 1;
    return lay == e6e0 && !e6e4 && (w >> 24 & 1);
}
/* the contact normal's Z of a word: FUN_1802538d0 [2] (raster/tb.c). With the flag (bit 24): t = (w & 0xffffff) x 5.960464832810452e-08, nz = (1 - t) /
   sqrt(t^2 + (1 - t)^2); a band layer k > 0 without it: the profile segment k - 1 -> k, nz = its first-coordinate step / its length - for the port's one band
   (the cylinder above the ball, C3_CT_CYL) 0. The words without the flag on layer 0 (the contact point's radius over the profile's +0 radius) are not made by
   the port's drop (McHmm's ball, profile type 3, always sets the flag: FUN_180126130 / FUN_180126e90 / FUN_180127d50). */
static double wl_ct_nz(unsigned w) {
    if (w >> 24 & 1) { double t = (double)(w & 0xffffff) * 5.960464832810452e-08; return (1.0 - t) / sqrt(t * t + (1.0 - t) * (1.0 - t)); }
    return 0.0;
}
/* the keep flags of a closed loop's vertices (above). ct (may be NULL): the vertices' contact words (wv_loops): a clipped one (0x80000000) is dropped, and a run
   holding one is put back only when shorter than PassesJoinGaps (FUN_18040b6f0 tests the flag before the window, hz l.11880-11890 / l.11990-12000), a
   contact dropped by FUN_1801344c0 likewise, and the window takes the word's normal (wl_ct_nz); without ct (the flat-radius offset polygons, which carry no
   words) the normal from the mesh (wl_contact_nz, ours). lim = the window test is on (a limit other than 0..90: with 0..90 the window [-0.1, 1.1] keeps every
   contact); e6e0 / e6e4: FUN_1801344c0's arguments (wl_ct_layer_drop). */
static void wl_angle_keep(const WlTg *g, const CPath *L, double zc, double Fo, double Rb, const double *cw, double joingap, const unsigned *ct, int e6e0, int e6e4, int lim, char *keep) {
    int n = L->n; double *nz = malloc(sizeof(double) * n);
    int meshnz = getenv("MCAM_WL_MESHNZ") != NULL;           /* testing: the mesh normal for the weave's vertices too (the port before 2026-11-14) */
    for (int q = 0; q < n; q++) {
        if (ct && ((ct[q] & 0x80000000u) || (!meshnz && wl_ct_layer_drop(ct[q], e6e0, e6e4)))) nz[q] = -1e300;   /* dropped, and hard for the restore */
        else if (!lim) nz[q] = 0.5;
        else nz[q] = ct && !meshnz ? wl_ct_nz(ct[q]) : wl_contact_nz(g, L->xy[2 * q], L->xy[2 * q + 1], zc, Fo, Rb);
        if (getenv("MCAM_WL_CTDUMP")) fprintf(stderr, "wlct %.9f %.9f %08x %.9f %.9f\n", L->xy[2 * q], L->xy[2 * q + 1], ct ? ct[q] : 0u, nz[q], zc - Rb);   /* testing */
        keep[q] = nz[q] >= cw[0] && nz[q] <= cw[1]; }
    int s = -1; for (int q = 0; q < n; q++) if (keep[q]) { s = q; break; }
    if (s >= 0) for (int c = 0; c < n; ) {                           /* walk the gaps from the first kept vertex */
        int a = (s + c) % n;
        if (!keep[a]) { c++; continue; }
        int d = 1; while (d <= n && !keep[(a + d) % n]) d++;
        if (d > 1) {
            int hard = 0; double len = 0;
            for (int t = 1; t <= d; t++) { int u = (a + t) % n, w = (a + t - 1) % n; len += hypot(L->xy[2 * u] - L->xy[2 * w], L->xy[2 * u + 1] - L->xy[2 * w + 1]);
                if (t < d && (nz[u] < cw[2] || nz[u] > cw[3])) hard = 1; }
            if (!hard || len < joingap) for (int t = 1; t < d; t++) keep[(a + t) % n] = 1;
        }
        c += d;
    }
    if (getenv("MCAM_WL_CTDUMP")) { int nk = 0, nr = 0; for (int q = 0; q < n; q++) { nk += keep[q]; nr += keep[q] && !keep[(q + n - 1) % n]; }   /* testing */
        fprintf(stderr, "wlck z %.9f n %d kept %d runs %d\n", zc - Rb, n, nk, nr);
        for (int q = 0; q < n; q++) if (!keep[q]) fprintf(stderr, "wlcd %.9f %.9f %08x\n", L->xy[2 * q], L->xy[2 * q + 1], ct ? ct[q] : 0u); }
    free(nz);
}
/* MCAM_HMM_DUMP=PREFIX (testing, tools/hmm_oracle.py, formats/hst_linker.md 16-17): the drive mesh as PREFIX.stl (mm), the op record as
   hst_record gives it (PREFIX.rec) and the job values the macro writer tools/hmm_macro.py needs besides the record (PREFIX.job: the unit
   system and the drive's lowest / highest Z - hstJobIdDat +0x28 / +0x30 = the tessellation's vertex range, mcmsurf FUN_180258310 - here of
   this port's mesh, op units) */
static void hmm_dump_stl(const char *suffix, const C3Mesh *M, double mf) {
    char pth[1024]; snprintf(pth, sizeof pth, "%s%s", getenv("MCAM_HMM_DUMP"), suffix); remove(pth);
    if (!M) return;
    if (M->attr) {   /* MCAM_HST_MESH: the binary tmphardmillraw.stl as mcmsurf FUN_180258310 writes it (formats/hst_linker.md 21) */
        FILE *so = fopen(pth, "wb");
        if (!so) return;
        char hd[0x51]; snprintf(hd, sizeof hd, "%-80s", "MASTERCAM STL DATA"); fwrite(hd, 1, 0x50, so);   /* l.395765-395769 */
        uint32_t cnt = (uint32_t)M->n; fwrite(&cnt, 4, 1, so);
        for (int t = 0; t < M->n; t++) {
            float P[3][3];
            for (int v = 0; v < 3; v++) for (int k = 0; k < 3; k++) P[v][k] = (float)(M->t[9 * t + 3 * v + k] * mf);
            /* FUN_1802284c0 (l.362853): the facet normal from the float corners' differences in double */
            double d3 = (double)(P[0][0] - P[1][0]), d4 = (double)(P[0][1] - P[1][1]), d10 = (double)(P[1][2] - P[2][2]), d1 = (double)(P[0][2] - P[1][2]);
            double d2 = (double)(P[1][0] - P[2][0]), d7 = (double)(P[1][1] - P[2][1]);
            float N[3] = { 0.0f, 0.0f, 1.0f };
            double m1 = fabs(d3) <= fabs(d4) ? fabs(d4) : fabs(d3); if (m1 <= fabs(d1)) m1 = fabs(d1);
            double m2 = fabs(d2) <= fabs(d7) ? fabs(d7) : fabs(d2); if (m2 <= fabs(d10)) m2 = fabs(d10);
            if (1e-12 <= m1 && 1e-12 <= m2) {
                float a = (float)(d10 * d4 - d7 * d1), b = (float)(d2 * d1 - d10 * d3), c = (float)(d7 * d3 - d2 * d4);
                float l = sqrtf(b * b + a * a + c * c);
                if (l < 1e-12f) { N[0] = 0.0f; N[1] = 0.0f; N[2] = 1.0f; } else { N[0] = a / l; N[1] = b / l; N[2] = c / l; }
            }
            fwrite(N, 4, 3, so); fwrite(P, 4, 9, so); fwrite(&M->attr[t], 2, 1, so);
        }
        fclose(so);
        return;
    }
    FILE *so = fopen(pth, "w");
    if (so) { fprintf(so, "solid drive\n");
        for (int t = 0; t < M->n; t++) { fprintf(so, " facet normal 0 0 0\n  outer loop\n");
            for (int v = 0; v < 3; v++) fprintf(so, "   vertex %.12g %.12g %.12g\n", M->t[9 * t + 3 * v] * mf, M->t[9 * t + 3 * v + 1] * mf, M->t[9 * t + 3 * v + 2] * mf);
            fprintf(so, "  endloop\n endfacet\n"); }
        fprintf(so, "endsolid drive\n"); fclose(so); }
}
/* MCAM_MESH_STL=FILE (testing): Raster's drive mesh replaced by an ASCII STL in mm (the format hmm_dump_stl writes, the file the mchmm.dll harness
   loads with the Database Loader plan 0x1421, tools/hmm_oracle.py) - a synthetic model given to the port and to the DLL alike (formats/hst_linker.md 21) */
static int c3_mesh_stl(const char *path, double mf, C3Mesh *M) {
    FILE *fi = fopen(path, "r"); if (!fi) return 0;
    char ln[512]; int nv = 0, cap = 0; double *t = NULL;
    while (fgets(ln, sizeof ln, fi)) { double x, y, z; char *v = strstr(ln, "vertex");
        if (!v || sscanf(v + 6, "%lf %lf %lf", &x, &y, &z) != 3) continue;
        if (nv == cap) { cap = cap ? 2 * cap : 768; t = realloc(t, sizeof(double) * 3 * cap); }
        t[3 * nv] = x / mf; t[3 * nv + 1] = y / mf; t[3 * nv + 2] = z / mf; nv++; }
    fclose(fi);
    if (nv < 3 || nv % 3) { free(t); return 0; }
    c3_mesh_free(M); M->t = t; M->n = nv / 3;
    for (int k = 0; k < 3; k++) { M->lo[k] = 1e300; M->hi[k] = -1e300; }
    for (int i = 0; i < nv; i++) for (int k = 0; k < 3; k++) { double v = t[3 * i + k]; if (v < M->lo[k]) M->lo[k] = v; if (v > M->hi[k]) M->hi[k] = v; }
    return 1;
}
/* MCAM_TC=FILE (testing): Raster's containment (as tmptc.tmp holds it) replaced by the loops of FILE in hmm_dump_tc's format ("n" then n lines "x y",
   op units) - with MCAM_MESH_STL a synthetic case given to the port and, through PREFIX.tc, to the mchmm.dll harness alike */
static int c3_read_tc(const char *path, CPaths *r) {
    FILE *fi = fopen(path, "r"); if (!fi) return 0;
    int n; memset(r, 0, sizeof *r);
    while (fscanf(fi, "%d", &n) == 1 && n > 0) {
        r->p = realloc(r->p, sizeof(CPath) * (r->n + 1)); CPath *c = &r->p[r->n++]; c->n = n; c->xy = malloc(sizeof(double) * 2 * n);
        for (int k = 0; k < 2 * n; k++) if (fscanf(fi, "%lf", &c->xy[k]) != 1) { fclose(fi); return 0; }
    }
    fclose(fi);
    return r->n > 0;
}
/* chk: the check model (c3_chk_open) or NULL - written as PREFIX.chk.stl (mckernel's check loader FUN_18007ca68 reads tmpmn5.emm, FUN_18006ea44) */
static void hmm_dump(const McamFile *f, const CamParams *p, const C3Mesh *M, int metric, double D, double Rc, const C3Chk *chk) {
    char pth[1024]; double mf = metric ? 1.0 : 25.4; const char *pre = getenv("MCAM_HMM_DUMP");
    hmm_dump_stl(".stl", M, mf);
    hmm_dump_stl(".chk.stl", chk ? c3_chk_mesh(chk) : NULL, mf);
    CamParams q = *p; q.nrpatch = 0;   /* the stored record (without --rec patches): the op as Mastercam ran it */
    uint32_t rn = 0; uint8_t *r = hst_record(f, &q, NULL, &rn);
    snprintf(pth, sizeof pth, "%s.rec", pre); FILE *ro = r ? fopen(pth, "wb") : NULL;
    if (ro) { fwrite(r, 1, rn, ro); fclose(ro); }
    free(r);
    snprintf(pth, sizeof pth, "%s.job", pre); FILE *jo = fopen(pth, "w");
    if (jo) { fprintf(jo, "metric %d\nzlo %.12g\nzhi %.12g\ndia %.12g\ncr %.12g\n", metric, M->lo[2], M->hi[2], D, Rc); fclose(jo); }
}
/* MCAM_HMM_DUMP: the containment as tmptc.tmp holds it (c3_tcb_offset), PREFIX.tc ("n" then n lines "x y" per loop, op units); none -> no file */
static void hmm_dump_tc(const CPaths *r) {
    char pth[1024]; snprintf(pth, sizeof pth, "%s.tc", getenv("MCAM_HMM_DUMP")); remove(pth);
    if (!r->n) return;
    FILE *o = fopen(pth, "w"); if (!o) return;
    for (int i = 0; i < r->n; i++) { fprintf(o, "%d\n", r->p[i].n); for (int k = 0; k < r->p[i].n; k++) fprintf(o, "%.12g %.12g\n", r->p[i].xy[2 * k], r->p[i].xy[2 * k + 1]); }
    fclose(o);
}
/* The Waterline Linking Plan's pass order (EvaGeneralLinkerPlan kind 0; formats/hst_linker.md 21; hz = decomp/hmm_zplan.c, hk = decomp/hmm_linker.c,
   x1 / x2 = decomp/hmm_linker_x1.c / x2.c). Checked with mchmm.dll (tools/tests/wl_order_model.py on the DLL's passes and its traced region graph:
   TUTORIAL 2 SWARF op 4 65 / 65, MOLD BOTTLE op 4 98 / 98 passes in the DLL's order).
   - The graph (FUN_1803ffdb0 hz l.3903 -> FUN_180400db0 hz l.4510): one node per region of a level (FUN_1802ed200; its passes = the frags of the region's
     toolpath, FUN_18026fb30), an edge node -> parent for every previous-level region FUN_1803fe090 (hz l.2672) finds: a point inside the region near
     each of its curves (FUN_1803fedb0 hz l.3330: the curve's mid-length point moved along the right-hand normal by 0.1, 0.05, 0.025, 0.0125 mm until
     FUN_1802887a0 says it lies inside the region) and the first previous region whose box and polygon hold it (FUN_1803ff7b0 hz l.3648-3708). Traced:
     FUN_1802ed2c0 from 180400f11 only (no 0x1bd5 branch). Here a region of a level = the free side of the level's loops: the loops whose probe has
     the same innermost containing loop of that level (ours: the port's slice gives loops, McHmm's plan gives regions; the graph so built equals
     the traced one on both oracle ops, formats/hst_linker.md 21.2). The probe's fallback (no inside point: hz l.3368-3640) is NOT DECODED: refused
     (the old order).
   - The lists (FUN_1801ba430 / FUN_1801ba970 / FUN_1801c98a0, hst_linker.md 19.1): +0x110 of a pass P = the passes of the child regions (the graph's
     predecessors FUN_180184200, walking through regions without passes) that overlap P (FUN_1801c2300 / FUN_18027a8d0, rp_overlap) at
     LinkORAdjacentLevelOverlap 0x1b13 = 2.2 x ToolShaftRadius (the Waterline plan's default, decomp/hmm_link.c l.18719-18726; ToolShaftRadius =
     radius + ToolThicknessXY - ToolThicknessZ); +0x340 = the inverse (the prerequisites).
   - Next pass (FUN_1801b90d0 hk l.27841): (1) FUN_1801c2d70 -> FUN_1801c2f40: the previous pass's +0x110 minus the taken ones (traced on op 4: one
     candidate list per pass), chosen by FUN_1801b9820 / FUN_1801b9cc0 / FUN_1801b9d70: the nearest by FUN_18027a4f0 (squared 3D distance to the start;
     a closed pass: to its nearest point FUN_18027a590 -> FUN_18027d4e0) from the previous pass's end; (2)-(4) none: the nearest of the free passes
     (rp_next's rule; the first pass: FUN_1801b9820 from (LinkStartX, LinkStartY, LinkSafeZ) with the walk over the candidates whose end lies within
     LinkMaxStayOnSurface of N's start, a closed one ending it, kind 0: a graph neighbour). Then FUN_1801b9490 (hk l.27988): a blocked choice
     (FUN_1801bf440) is replaced by the nearest (FUN_1801c9500 / FUN_1801c9390, from the previous end) of its not taken prerequisites that are free,
     followed down the prerequisite chains (traced: 93 -> 82 / 83 -> 83 on op 4).
   - The entry of a closed pass (FUN_1801c05c0 hk l.33400-33436): re-split at its nearest point S to the previous end (FUN_18027a590 / FUN_180281580);
     a stay-down link (XY gap < LinkMaxStayOnSurface hk l.33551, the next pass's region a child of the previous one's, hk l.33560-33608) with
     SurfaceRampStyle 1 is a surface ramp (FUN_1801c3dd0 x1 l.1914): the next pass trimmed at its start by |dZ| / tan(SurfaceRampAngle)
     (hk l.35788-35795), so a closed pass then starts and ends that far along the loop from S (measured: op 4 loops 15 / 16, DLL moves 4953 / 5729);
     that end is the next choice's reference. The link moves themselves (the ramp, its smoothing, the leads, the retracts) are NOT DECODED here:
     the passes are joined by the SAFE rule (ours, labelled), an open pass is not trimmed (its ramp would cut the trimmed piece). */
typedef struct { double *p; int n, closed, node; int *succ, nsucc, *pre, npre; double *item; int nitem;
                 double *rg; int *rgn, nrg; } WlPass;   /* item: an open pass's closed toolpath item (mm, closing point included), FUN_18027b580;
                                                         rg / rgn / nrg: the region loops of the pass's level (mm, x y pairs, nrg loops of rgn[k] points;
                                                         the toolpath's GridC 0x1e, FUN_1801a1240 / FUN_180176540 below) */
typedef struct { int level, h, npar, *par; double z; } WlNode;
typedef struct WlLead WlLead;
static int wl_arctan;   /* wl_tan below: MCAM_HMM_PASSES (testing) */   /* z: the region's level (mm; NAN = unknown), FUN_1801af3d0's parent toolpath +0x88 */
struct WlLead {
    C3Link *lk;                      /* the check connects (FUN_1801d1c20, layers 0): chord test FUN_1801ce270 = c3_lc_chord, drop FUN_1801cf7b0 = c3_lc_drop */
    double rin, rout;                /* HorizontalLeadIn / OutRadius (plan +0x11f0 / +0x11f8; record 0x1368 / 0x1370, mk l.90129-90160) */
    double rvin, rvout;              /* LinkVerticalLeadIn / OutRadius (+0x1128 / +0x1130; record 0x1348 / 0x1350) */
    double off;                      /* LinkRampHeightOffset (+0x1170; the feed plane, record 0x190) */
    double ain, aout;                /* MaxRampAngle (+0x11e0, radians) and LiftAngle (+0x11e8, plan default 0: mckernel does not write it) */
    float rho;                       /* RampHeightOffset (+0x11a4, a float; record 0x1420) */
    double sdown;                    /* plan +0x1240 = max(Stepdown 0x1ada, MachStepover 0x1ad9) (hl l.4892-4899); traced 1.27 / 0 on op 4 */
    double clr;                      /* LinkZClearance (linker +0x1e8) */
    double atol;                     /* 0.45 x the cut tolerance (record 0x12a0), mm: mccore's arc linearisation FUN_1809187f0 (hst_linker.md 13.4) */
    int on;                          /* the decoded case: LinkRetractStyle 2 (+0x1108 / +0x1109 set), both horizontal radii > 0, LiftAngle 0 */
    int tstyle; double maxtrim;      /* LinkTrimmingStyle (0x1b1c) and LinkMaxAllowedTrimming (+0x1110, mm): the checks' retry step, FUN_18019d5d0's attempts */
    int nret, nappr, nsafe;          /* retracts / first approaches emitted, links kept SAFE */
    int nfall, nramp, nexit;         /* of those: lead-outs by the fallback FUN_18019d5d0, lead-ins by the ramp FUN_180176760; final exits */
    int dbg;
};
static int wl_pip(const CPath *P, double x, double y) {
    int in = 0, n = P->n;
    for (int i = 0, j = n - 1; i < n; j = i++) { double xi = P->xy[2 * i], yi = P->xy[2 * i + 1], xj = P->xy[2 * j], yj = P->xy[2 * j + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in; }
    return in;
}
/* the innermost loop of L holding (x, y) (the one with the fewest containing loops among them is the outermost: here the one held by the most) */
static int wl_inner(const CPaths *L, double x, double y, const int *depth) {
    int best = -1;
    for (int i = 0; i < L->n; i++) if (L->p[i].n >= 3 && wl_pip(&L->p[i], x, y) && (best < 0 || depth[i] > depth[best])) best = i;
    return best;
}
static void wl_depths(const CPaths *L, int *depth) {
    for (int i = 0; i < L->n; i++) { depth[i] = 0; if (L->p[i].n < 3) continue;
        for (int j = 0; j < L->n; j++) if (j != i && L->p[j].n >= 3 && wl_pip(&L->p[j], L->p[i].xy[0], L->p[i].xy[1])) depth[i]++; }
}
/* FUN_1803fedb0's probe of loop i run in direction r (r: vertices backwards): the mid-length point moved along the right-hand normal by s = 0.1 mm halving
   while > 0.01 mm, kept when its innermost loop is not i itself... the region test: the probe must lie on the free side, i.e. outside loop i if i is
   its own container, else inside; returns 0 when no step gives a point (NOT DECODED fallback) */
static int wl_probe(const CPaths *L, int i, int r, const int *depth, double mm, double *q, int *h) {
    const CPath *P = &L->p[i]; int n = P->n; if (n < 3) return 0;
    double len = 0; for (int k = 0; k < n; k++) { int a = k, c = (k + 1) % n; len += hypot(P->xy[2 * c] - P->xy[2 * a], P->xy[2 * c + 1] - P->xy[2 * a + 1]); }
    double half = len * 0.5, acc = 0;
    for (int k = 0; k < n; k++) {
        int a = r ? (n - k) % n : k, c = r ? (n - k - 1 + n) % n : (k + 1) % n;
        double ax = P->xy[2 * a], ay = P->xy[2 * a + 1], dx = P->xy[2 * c] - ax, dy = P->xy[2 * c + 1] - ay, sl = hypot(dx, dy);
        if (!(sl > 0)) continue;
        if (acc + sl < half && k < n - 1) { acc += sl; continue; }
        double t = (half - acc) / sl; if (t > 1) t = 1; if (t < 0) t = 0;
        double mx = ax + t * dx, my = ay + t * dy, nx = dy / sl, ny = -dx / sl;   /* right-hand normal */
        for (double s = 0.1; s > 0.01; s *= 0.5) {
            double x = mx + nx * s * mm, y = my + ny * s * mm; int in_i = wl_pip(P, x, y);
            int hh = wl_inner(L, x, y, depth);
            /* the probe is in the loop's region: on the loop's free side - outside the loop when the loop bounds the region from inside (an island), inside it
               when the loop is the region's outer boundary (hh == i) */
            if ((hh == i && in_i) || (hh != i && !in_i)) { q[0] = x; q[1] = y; *h = hh; return 1; }
        }
        return 0;
    }
    return 0;
}
static int wl_child_has(const WlNode *N, int nn, const int *npass, int parent, int child) {   /* FUN_180184200: child's region among parent's predecessors, through regions without passes */
    int *todo = malloc(sizeof(int) * (nn + 1)), nt = 0, found = 0; char *seen = calloc(nn, 1);
    for (int c = 0; c < nn; c++) for (int k = 0; k < N[c].npar; k++) if (N[c].par[k] == parent && !seen[c]) { seen[c] = 1; todo[nt++] = c; }
    for (int t = 0; t < nt && !found; t++) { int c = todo[t];
        if (npass[c]) { if (c == child) found = 1; continue; }
        for (int d = 0; d < nn; d++) for (int k = 0; k < N[d].npar; k++) if (N[d].par[k] == c && !seen[d]) { seen[d] = 1; todo[nt++] = d; } }
    free(todo); free(seen);
    return found;
}
static double wl_d3(const double *a, const double *b) { double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }
/* FUN_18027d4e0 (flag 0): the nearest point of the polyline (per segment the interior foot, then the end vertex, the first vertex once), 3D */
static void wl_npt3(const double *p, int n, const double *q, double *out, int *seg, double *tt) {
    double bd = -1; *seg = 0; *tt = 0; memcpy(out, p, sizeof(double) * 3);
    for (int i = 0; i + 1 < n; i++) {
        const double *a = p + 3 * i, *b = a + 3; double e[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, L = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
        double t = L == 0 ? 0.5 : ((q[0] - a[0]) * e[0] + (q[1] - a[1]) * e[1] + (q[2] - a[2]) * e[2]) / L;
        if (t > 0 && t < 1) { double f[3] = { a[0] + t * e[0], a[1] + t * e[1], a[2] + t * e[2] }, d = wl_d3(f, q); if (bd < 0 || d < bd) { bd = d; memcpy(out, f, sizeof f); *seg = i; *tt = t; } }
        double d = wl_d3(b, q); if (bd < 0 || d < bd) { bd = d; memcpy(out, b, sizeof(double) * 3); *seg = i; *tt = 1; }
        if (i == 0) { d = wl_d3(a, q); if (bd < 0 || d < bd) { bd = d; memcpy(out, a, sizeof(double) * 3); *seg = 0; *tt = 0; } }
    }
}
static double wl_score(const WlPass *P, const double *ref) {
    if (!P->closed) return wl_d3(P->p, ref);
    double q[3]; int s; double t; wl_npt3(P->p, P->n, ref, q, &s, &t); return wl_d3(q, ref);
}
static int wl_blocked(const WlPass *W, const char *used, int f) { for (int k = 0; k < W[f].npre; k++) if (!used[W[f].pre[k]]) return 1; return 0; }
static void wl_free_anc(const WlPass *W, const char *used, int f, int *out, int *no, int depth) {
    if (depth > 10000) return;
    for (int k = 0; k < W[f].npre; k++) { int p = W[f].pre[k]; if (used[p]) continue;
        if (wl_blocked(W, used, p)) wl_free_anc(W, used, p, out, no, depth + 1);
        else { int dup = 0; for (int j = 0; j < *no; j++) if (out[j] == p) dup = 1; if (!dup) out[(*no)++] = p; } }
}
static int wl_best(const WlPass *W, const int *c, int nc, const double *ref) {
    int best = -1; double bs = 0;
    for (int i = 0; i < nc; i++) { double s = wl_score(&W[c[i]], ref); if (best < 0 || s < bs || (s == bs && c[i] < best)) { best = c[i]; bs = s; } }
    return best;
}
/* the closed polyline p rotated to start (and end) at the point dist along it from its nearest point to ref; out has n + 1 points */
static int wl_resplit(const double *p, int n, const double *ref, double dist, double *out) {
    double S[3]; int seg; double t; wl_npt3(p, n, ref, S, &seg, &t);
    if (t >= 1) { seg++; t = 0; if (seg >= n - 1) seg = 0; }
    double rem = dist, pos[3]; memcpy(pos, S, sizeof pos);
    for (int guard = 0; rem > 0 && guard < 2 * n; guard++) {
        const double *b = p + 3 * (seg + 1); double L = sqrt(wl_d3(pos, b));
        if (L >= rem) { for (int i = 0; i < 3; i++) pos[i] += (b[i] - pos[i]) * rem / L; rem = 0; break; }
        rem -= L; seg = (seg + 1) % (n - 1); memcpy(pos, p + 3 * seg, sizeof pos);
    }
    int m = 0; memcpy(out, pos, sizeof pos); m++;
    for (int k = 1; k < n; k++) { int v = (seg + k) % (n - 1); memcpy(out + 3 * m, p + 3 * v, sizeof(double) * 3); m++; }
    memcpy(out + 3 * m, pos, sizeof pos); m++;
    return m;
}
static void wl_link(WlPass *W, int nw, const WlNode *N, int nn, int fail, MB *b, double fp, double plunge, double feed, int *down, double u, double dov,
                    double maxstay, double safez, int tr, double ramp, int *nloops, double cx, double cy, C3StayDown *sdw, WlLead *wv);   /* below rp_next */
static void wl_link_hst(WlPass *W, int nw, const WlNode *N, int nn, int fail, MB *b, double fp, double plunge, double feed, int *down, double u, const C3Mesh *M,
                        double R, double Rc, double sxy, double sz, double tol, double D, double wl_maxstay, double wl_safez, int wl_tr, double wl_ramp,
                        int *nloops, double cx, double cy, WlLead *wv);   /* below wl_link */
/* ---- The flat-radius offset by McHmm's own chain (formats/hst3d_port_plan.md 7 "W8"; hz = decomp/hmm_zplan.c, hzp = decomp/hmm_zprofile.c,
   hw = decomp/hst3d_weave_drop.c). Per weave level FUN_1803faee0 (hz l.960-1090) runs, for each pocket of the weave (Eva +0x1430 +0x240, made by
   FUN_18012b1e0: cam3d_rough.c WvPockets), the angle filter FUN_18040b6f0, which writes the pocket's loops through the writer at Eva +0x1440 with each
   vertex's keep byte (hz l.12262-12345: FUN_180118ee0 the start node, kind 0xf for the pocket's first loop else 0xe; FUN_1801194d0 every next node
   except one whose next node lies exactly on the current one's point; FUN_180121900), and then FUN_180407c10 (hzp l.1-707): FUN_18011cbc0(Eva +0x1440,
   .., 1) (thinning + the TGridC records with the element flags as vertex bits, hw l.8091), the records after the first with exactly 3 entries dropped
   (hzp l.153-172), FUN_180409f40 (FGridC copy), FUN_180406660 (the first polygon clockwise, the others counter-clockwise; else "AllPolygonsChecked
   failed" and the fallback of hzp l.263-586, NOT DECODED), the construction (hmm_voronoi.c hv_build), r' = FUN_18054a640(VorProfileFlatRad Eva +0xff8,
   RadPushUp, RadPushUpLimit) when that is not 0 (hv_push_radius), the regions FUN_18054b480 (hv_regions) and per region FUN_180406b00 kind 0: the
   compact copy (hv_copy, its +0x78 = VoronoiPolyEpsilon), FUN_180555f60 + FUN_180557f60 per loop (hv_offset_region) through the writer at Eva +0x4f0
   (hmm_pipethin.c, FUN_180406b00's set-up: hpt_new) and its flush FUN_18011ca00 (hpt_flush) - each record one pass, its vertex bits the span flags.
   Traced on TUTORIAL 2 SWARF op 4 (peloader --hmm, HMM_TRACE 18040b6f0:W / 180557f60:X:s5:o78:O): Eva +0xff8 = 3.175 mm (ToolShaftRadius -
   ToolCornerRadius), +0x1001 = 0, +0x15c0 = 0, +0x1008 / +0x1010 / +0x1018 = 0.00015 / 0.001 / 0.0001, the template +0x4a8 = {2, 0.0127, cos 45 deg,
   0.0005, -1, -1, 1, cos / sin 0.1 deg}, FUN_180557f60's MaxRadius 0.635 (= 0.05 D), MinRadius 0.0001, MinArcChordLength 0.00015, MinDeviation 0.001,
   param_9 0, copy +0x78 = 2e-5, t = tau = 0. Everything in McHmm's mm (job +0x80). */
typedef struct {
    HptTemplate t1, t2;      /* the filter writer's template (FUN_1803faee0 hz l.968-1000) and the offset writer's (Eva +0x4a8, FUN_1802f2340) */
    int e15c0, pco;          /* Eva +0x15c0 (the filter writer's flag rule); PosContactOnly (the offset writer's +0x114) */
    HvParams hp;             /* VoronoiParallelVanishingDistanceReciprocal 0x1bc7, -SmallBisectorLength 0x1bc8, -Split 0x1bc9, -PerturbationTolerance 0x1bc6 */
    double r0, pushup, pushlim;                      /* VorProfileFlatRad (Eva +0xff8), RadPushUp 0x1bca, RadPushUpLimit 0x1bcb */
    double tau, mindev, maxrad, minrad, minchord, peps;   /* CornerTolerance (0 without ProfileSmoothing), Eva +0x1010, 0x1bd2, Eva +0x1018, Eva +0x1008, 0x1bce */
    double steptol;          /* StepMachTolerance 0x1acc (mm): FUN_1803faee0's retry bound */
} WlVor;
/* the passes of one level: polylines (op units; arcs as chords) with, per vertex, whether the span ending there is kept (FUN_180249cc0 / FUN_180017470's
   rule: a span whose end vertex carries exactly 0x40000000 is skipped; an arc's span is judged by the vertex after its centre) and the record's closed flag */
typedef struct { CPaths L; char **sp; char *closed; } WlVorOut;
static void wlvor_out_free(WlVorOut *o) { for (int i = 0; i < o->L.n; i++) free(o->sp[i]); free(o->sp); free(o->closed); cl_free(&o->L); memset(o, 0, sizeof *o); }
static void wlvor_push(WlVorOut *o, double *xy, char *sp, int n, int closed) {
    if (o->L.n % 32 == 0) { o->L.p = realloc(o->L.p, sizeof(CPath) * (o->L.n + 32)); o->sp = realloc(o->sp, sizeof(char *) * (o->L.n + 32)); o->closed = realloc(o->closed, o->L.n + 32); }
    o->L.p[o->L.n].xy = xy; o->L.p[o->L.n].n = n; o->sp[o->L.n] = sp; o->closed[o->L.n] = (char)closed; o->L.n++;
}
/* FUN_18005c800's sign as FUN_180406660 uses it (-1 clockwise / +1 counter-clockwise): read as the sign of the polygon's area (NOT verified against the DLL's
   function beyond the traced ops passing the check) */
/* MCAM_WL_WDUMP (testing): the offset writer's calls, as peloader HMM_TRACE 1801192a0:v,1801194d0:v,1801195f0:v shows the DLL's */
typedef struct { HvWriter w; } WlWd;
static void wlwd_start(void *c, const double *p, int f, int k) { WlWd *d = c; fprintf(stderr, "WS %.17g %.17g f%d k%x\n", p[0], p[1], f != 0, k); d->w.start(d->w.ctx, p, f, k); }
static void wlwd_line(void *c, const double *p, int f) { WlWd *d = c; fprintf(stderr, "WL %.17g %.17g f%d\n", p[0], p[1], f != 0); d->w.line(d->w.ctx, p, f); }
static void wlwd_arc(void *c, const double *p, const double *ce, double r, int b5, int b6, int f) { WlWd *d = c; fprintf(stderr, "WA %.17g %.17g c %.17g %.17g r %.17g %d %d f%d\n", p[0], p[1], ce[0], ce[1], r, b5, b6, f != 0); d->w.arc(d->w.ctx, p, ce, r, b5, b6, f); }
static void wlwd_end(void *c) { WlWd *d = c; fprintf(stderr, "WE\n"); d->w.end(d->w.ctx); }
static int wlvor_orient(const double *xy, int n) { double a = 0; for (int i = 0; i < n; i++) { int j = (i + 1) % n; a += xy[2 * i] * xy[2 * j + 1] - xy[2 * j] * xy[2 * i + 1]; } return a > 0 ? 1 : a < 0 ? -1 : 0; }
/* the polygons of the filter writer's records (mm): FUN_180409f40's copy; drop3 = FUN_180407c10's first pass (hzp l.153-172: the records after the
   first with exactly 3 entries dropped). 0 = an open record (NOT DECODED) */
static int wlvor_polys(const HptPasses *R1, int drop3, HvVert **vvo, HvPoly **ppo, int *npo, char *err, int nerr) {
    int np = 0, tot = 0; for (int i = 0; i < R1->nrec; i++) tot += (int)R1->rec[i].n;
    HvVert *vv = malloc(sizeof(HvVert) * (tot + 1)); HvPoly *pp = malloc(sizeof(HvPoly) * (R1->nrec + 1)); int nv = 0;
    for (int i = 0; i < R1->nrec; i++) {
        const HptRec *r = &R1->rec[i];
        if (drop3 && i > 0 && r->n == 3) continue;
        if (!r->closed) { snprintf(err, nerr, "an open record in the angle filter's writer (FUN_180409f40's input): NOT DECODED"); free(vv); free(pp); return 0; }
        int m = (int)r->n - 1;                                             /* a closed record keeps its count, its last point (= the first) popped */
        for (int j = 0; j < m; j++) { vv[nv + j].x = R1->xy[2 * (r->first + j)]; vv[nv + j].y = R1->xy[2 * (r->first + j) + 1]; vv[nv + j].kind = R1->bits[r->vb + j] & 0xfffffffff0000000ull; }
        pp[np].v = vv + nv; pp[np].n = m; np++; nv += m;
    }
    *vvo = vv; *ppo = pp; *npo = np; return 1;
}
static void wlvor_case(const WlVor *V, const HvPoly *pp, int np, double z) {
    if (!getenv("MCAM_WL_VORCASE")) return;   /* testing: the pocket as a case of tools/peloader --hmmvor / tools/tests/hmm_voronoi_cli (hmm_vor.c's CASES format) */
    static int ncase; FILE *cf = fopen(getenv("MCAM_WL_VORCASE"), ncase ? "a" : "w");
    if (!cf) return;
    fprintf(cf, "CASE wl%d_z%.6f\nPAR %a %a %a %a\n", ncase++, z, V->hp.recip, V->hp.sbl, V->hp.sbs, V->hp.ptol);
    for (int k = 0; k < np; k++) { fprintf(cf, "POLY\n"); for (int j = 0; j < pp[k].n; j++) fprintf(cf, "V %a %a %llx\n", pp[k].v[j].x, pp[k].v[j].y, (unsigned long long)pp[k].v[j].kind); }
    fprintf(cf, "OFF %a %a %a\nVOFF %a %a %a %a %a 0 %a\nPIPE %a %d %a %a %a %a %a %a %a %a %d\nRUN\n", V->r0, V->pushup, V->pushlim, V->tau, V->mindev, V->maxrad, V->minrad, V->minchord, V->peps,
            z, V->t2.mode, V->t2.ctol, V->t2.cang, V->t2.smin, V->t2.smax, V->t2.amax, (double)V->t2.senab, V->t2.scos, V->t2.ssin, V->pco);
    fclose(cf);
}
/* FUN_18040b6f0's writer calls for a pocket's loops (hz l.12279-12341): FUN_180118ee0 the start node (kind 0xf for the pocket's first loop else 0xe),
   FUN_1801194d0 every next node except one whose next node lies exactly on the current one's point (a two-node ring always written), FUN_180121900 */
static void wlvor_write(const CPath *const *loops, char *const *keep, int nlp, HvWriter *w) {
    for (int k = 0; k < nlp; k++) {
        const CPath *L = loops[k]; int n = L->n; const char *kp = keep[k];
        double p[3] = { L->xy[0], L->xy[1], 0.0 };
        w->start(w->ctx, p, kp[0], k ? 0xe : 0xf);
        int cur = 0;
        do {
            int nx = (cur + 1) % n, n2 = (nx + 1) % n;
            if (n2 == cur || L->xy[2 * n2] != L->xy[2 * cur] || L->xy[2 * n2 + 1] != L->xy[2 * cur + 1]) {   /* FUN_1800082c0 on x and y (exact) */
                double q[3] = { L->xy[2 * nx], L->xy[2 * nx + 1], 0.0 }; w->line(w->ctx, q, kp[nx]); }
            cur = nx;
        } while (cur != 0);
        w->end(w->ctx);
    }
}
/* MCAM_WL_FIXCASE=FILE (testing): each pocket try whose construction fails, as a FIX case of tools/peloader --hmmvor (hmm_vor.c: the DLL's own writer,
   FUN_18011cbc0 and FUN_18011ce60 on the same calls) */
static void wlfc_start(void *c, const double *p, int f, int k) { fprintf(c, "LS %a %a %d %d\n", p[0], p[1], f != 0, k); }
static void wlfc_line(void *c, const double *p, int f) { fprintf(c, "LL %a %a %d\n", p[0], p[1], f != 0); }
static void wlfc_arc(void *c, const double *p, const double *ce, double r, int b5, int b6, int f) { (void)c; (void)p; (void)ce; (void)r; (void)b5; (void)b6; (void)f; }
static void wlfc_end(void *c) { fprintf(c, "LE\n"); }
static void wlvor_fixcase(const HptTemplate *t, int e15c0, double z, const CPath *const *loops, char *const *keep, int nlp) {
    static int nc; FILE *cf = fopen(getenv("MCAM_WL_FIXCASE"), nc ? "a" : "w"); if (!cf) return;
    fprintf(cf, "CASE fix%d_z%.6f_tol%.6g\nFIX %d %a %a %a %a %a %a %a %a %d\n", nc++, z, t->ctol, t->mode, t->ctol, t->cang, t->smin, t->smax, t->amax, (double)t->senab, t->scos, t->ssin, e15c0);
    HvWriter w = { cf, wlfc_start, wlfc_line, wlfc_arc, wlfc_end }; wlvor_write(loops, keep, nlp, &w);
    fprintf(cf, "RUN\n"); fclose(cf);
}
/* One try of FUN_1803faee0's loop (hz l.989-1065) at the filter tolerance ctol: FUN_18040b6f0 writes the pocket's loops into the writer at Eva +0x1440
   (the template of FUN_1803faee0 hz l.968-1000 with +8 = ctol), then FUN_180407c10 up to the construction (hzp l.135-262) and its fallback (hzp l.263-590):
   - the records (FUN_18011cbc0(.., 1), the curves kept), the 3-entry drop, FUN_180406660 (AllPolygonsChecked: polygon 0 clockwise, the others
     counter-clockwise; read as the area sign), the construction FUN_18054a440 (hv_build);
   - when either fails: the ToolBoxSet of the polygons after the first (FUN_180231f50 + FUN_180017470(.., 1) per FGridC, hzp l.264-409: their spans, the
     closing one included - FUN_18004f610 maps the last vertex of a closed record to its first, dis 18004f626-18004f638) and FUN_18011ce60 (hpt_fix_isect)
     on the writer's curves; it split something: the records again (FUN_18011cbc0(.., 1), hzp l.429-443: every record kept, no orientation check) and a
     second construction (hzp l.444-493, "GenerateVoronoi failed second level" when it fails: FUN_180407c10 then returns 0); it split nothing: FUN_180407c10
     returns !bVar32 (hzp l.584-587) = 1 after AllPolygonsChecked failed (no retry; the pocket killed: Eva +0x1430 +0x258 bit) and 0 after the
     construction failed (a retry).
   Returns 1 = built (*gp, *vvp, *ppp: the graph and its polygons), 2 = FUN_180407c10 returned 0 (FUN_1803faee0 retries), 3 = the pocket killed, 0 =
   refused (err). */
static int wlvor_try(const WlVor *V, double ctol, double z, const CPath *const *loops, char *const *keep, int nlp, HvGraph **gp, HvVert **vvp, HvPoly **ppp,
                     int *npp, const char **why, char *err, int nerr) {
    HptTemplate t1 = V->t1; t1.ctol = ctol;
    HvWriter w; HptWriter *W1 = hpt_new_filter(&t1, V->e15c0); hpt_hvwriter(W1, &w);
    wlvor_write(loops, keep, nlp, &w);
    HptPasses R1; memset(&R1, 0, sizeof R1); char msg[200];
    if (!hpt_records(W1, &R1, msg, sizeof msg)) { snprintf(err, nerr, "the angle filter's writer: %s", msg); hpt_passes_free(&R1); hpt_free(W1); return 0; }
    HvVert *vv; HvPoly *pp; int np;
    if (!wlvor_polys(&R1, 1, &vv, &pp, &np, err, nerr)) { hpt_passes_free(&R1); hpt_free(W1); return 0; }
    hpt_passes_free(&R1);
    if (!np) { free(vv); free(pp); hpt_free(W1); *gp = NULL; *vvp = NULL; *ppp = NULL; *npp = 0; return 1; }
    int orient_ok = 1;                                                    /* FUN_180406660 */
    for (int k = 0; k < np && orient_ok; k++) {
        double *xy = malloc(sizeof(double) * 2 * (pp[k].n > 0 ? pp[k].n : 1));
        for (int j = 0; j < pp[k].n; j++) { xy[2 * j] = pp[k].v[j].x; xy[2 * j + 1] = pp[k].v[j].y; }
        int o = pp[k].n >= 3 ? wlvor_orient(xy, pp[k].n) : 0; free(xy);
        if (o != (k ? 1 : -1)) orient_ok = 0;
    }
    if (orient_ok) {
        wlvor_case(V, pp, np, z);
        HvGraph *g = hv_new(&V->hp);
        if (hv_build(g, pp, np)) { hpt_free(W1); *gp = g; *vvp = vv; *ppp = pp; *npp = np; return 1; }
        hv_free(g);
    }
    /* the fallback: the ToolBoxSet of polygons 1.. (FUN_180017470 with param_4 = 1: every span) */
    if (getenv("MCAM_WL_FIXCASE")) wlvor_fixcase(&t1, V->e15c0, z, loops, keep, nlp);
    int nseg = 0; for (int k = 1; k < np; k++) nseg += pp[k].n;
    double *segs = malloc(sizeof(double) * 4 * (nseg + 1)); int q = 0;
    for (int k = 1; k < np; k++) for (int j = 0; j < pp[k].n; j++) { const HvVert *a = &pp[k].v[j], *b = &pp[k].v[(j + 1) % pp[k].n];
        segs[4 * q] = a->x; segs[4 * q + 1] = a->y; segs[4 * q + 2] = b->x; segs[4 * q + 3] = b->y; q++; }
    free(vv); free(pp);
    int fixed = hpt_fix_isect(W1, segs, nseg); free(segs);
    if (fixed < 0) { snprintf(err, nerr, "FUN_180407c10's fallback: FUN_18011ce60 would split a composite into an empty part (not reproducible) - refused"); hpt_free(W1); return 0; }
    if (fixed == 0) { hpt_free(W1); *why = orient_ok ? "the construction failed, FUN_18011ce60 found no crossing" : "AllPolygonsChecked failed, FUN_18011ce60 found no crossing"; return orient_ok ? 2 : 3; }
    HptPasses R2; memset(&R2, 0, sizeof R2);
    if (!hpt_records(W1, &R2, msg, sizeof msg)) { snprintf(err, nerr, "the angle filter's writer (second flush): %s", msg); hpt_passes_free(&R2); hpt_free(W1); return 0; }
    hpt_free(W1);
    if (!wlvor_polys(&R2, 0, &vv, &pp, &np, err, nerr)) { hpt_passes_free(&R2); return 0; }
    hpt_passes_free(&R2);
    wlvor_case(V, pp, np, z);
    HvGraph *g = hv_new(&V->hp);
    if (np && hv_build(g, pp, np)) { *gp = g; *vvp = vv; *ppp = pp; *npp = np; return 1; }
    hv_free(g); free(vv); free(pp);
    *why = "GenerateVoronoi failed second level"; return 2;
}
/* one pocket: loops = the pocket's loops (mm, McHmm's order, start node first), keep = their vertices' keep bytes (FUN_18040b6f0); z = the level (mm);
   u = mm per op unit; adds the passes to out (op units). 1 = done, 0 = a step NOT DECODED / NOT PORTED (err).
   FUN_1803faee0 (hz l.989-1065, dis 1803fb39b-1803fb3af: addsd xmm6, xmm7; comisd xmm8): while FUN_180407c10 returns 0 the tolerance grows by its first
   value (VoronoiInputCurveTolerance 0x1bc3) and the pocket is written again, until it exceeds StepMachTolerance 0x1acc (then message 0x4c3 with the level) */
static int wlvor_pocket(const WlVor *V, const CPath *const *loops, char *const *keep, int nlp, double z, double u, double chord_tol, WlVorOut *out, char *err, int nerr) {
    HvGraph *g = NULL; HvVert *vv = NULL; HvPoly *pp = NULL; int np = 0, rc = 0, ntry = 0; const char *why = NULL;
    for (double ctol = V->t1.ctol;; ctol += V->t1.ctol) {
        why = NULL;
        rc = wlvor_try(V, ctol, z, loops, keep, nlp, &g, &vv, &pp, &np, &why, err, nerr); ntry++;
        if (getenv("MCAM_WL_VORDUMP")) fprintf(stderr, "wlvor try z %.9f tol %.17g rc %d %s\n", z, ctol, rc, rc == 1 ? "" : why ? why : err);   /* testing */
        if (rc != 2) break;
        if (V->steptol < ctol + V->t1.ctol) break;
    }
    if (rc == 0) return 0;
    if (rc != 1) {
        /* the pocket gives no passes in the DLL (FUN_180407c10 cleared its pass vector, hzp l.136), but its marks (Eva +0x1430 +0x258 / +0x280 bits and
           +0x278, hzp l.561-590) steer FUN_1803fc9b0's weave bookkeeping (hz l.1881-2240: "part N killed", "Removing over offset pocket", the +0x278 branch
           with FUN_180400fc0) and through it the next levels - NOT DECODED */
        snprintf(err, nerr, "pocket's Voronoi failed (%s, %d tries): FUN_1803fc9b0 NOT DECODED", rc == 2 ? "0x4c3" : "killed", ntry);
        return 0;
    }
    if (!np) return 1;
    int ok = 1; char msg[200];
    double r = V->r0 != 0.0 ? hv_push_radius(g, V->r0, V->pushup, V->pushlim) : 0.0;   /* FUN_18054a640 (hzp l.592-602) */
    int nreg = hv_regions(g, r);
    for (int k = 0; k < nreg && ok; k++) {
        HvCopy *cp = hv_copy_new();
        if (!hv_copy(cp, g, k, r)) { snprintf(err, nerr, "the region copy FUN_18054ec00 asserts in the DLL here"); ok = 0; hv_copy_free(cp); break; }
        memcpy(hv_copy_raw(cp) + 0x78, &V->peps, 8);                     /* VoronoiPolyEpsilon (FUN_180406b00 hp l.1821-1855) */
        HptWriter *W2 = hpt_new(&V->t2, z, V->pco); HvWriter w2; hpt_hvwriter(W2, &w2);
        WlWd wd = { w2 }; HvWriter wdw = { &wd, wlwd_start, wlwd_line, wlwd_arc, wlwd_end };
        int rv = hv_offset_region(cp, getenv("MCAM_WL_WDUMP") ? &wdw : &w2, V->tau, V->mindev, V->maxrad, V->minrad, V->minchord, 0);
        hv_copy_free(cp);
        if (rv != 1) { snprintf(err, nerr, "the offset loop FUN_180557f60 asserts in the DLL here"); hpt_free(W2); ok = 0; break; }
        HptPasses R2; memset(&R2, 0, sizeof R2);
        if (!hpt_flush(W2, &R2, msg, sizeof msg)) { snprintf(err, nerr, "the offset writer: %s", msg); hpt_free(W2); hpt_passes_free(&R2); ok = 0; break; }
        hpt_free(W2);
        for (int i = 0; i < R2.nrec; i++) {   /* the record as a polyline (op units): its vertices, an arc's centre vertex (bits 0x4000000 / 0x5000000) replaced by chords */
            const HptRec *rc = &R2.rec[i]; int m = (int)rc->n, cap = m + 16, q = 0;
            double *xy = malloc(sizeof(double) * 2 * cap); char *sp = malloc(cap);
            #define WV_ADD(X, Y, S) do { if (q == cap) { cap *= 2; xy = realloc(xy, sizeof(double) * 2 * cap); sp = realloc(sp, cap); } xy[2 * q] = (X) / u; xy[2 * q + 1] = (Y) / u; sp[q] = (char)(S); q++; } while (0)
            #define WV_PT(j, c) (((j) < m - 1 || !rc->closed) ? R2.xy[2 * (rc->first + (j)) + (c)] : R2.xy[2 * rc->first + (c)])
            for (int j = 0; j < m; j++) {
                uint64_t bt = R2.bits[rc->vb + j] & 0xfffffffff0000000ull, b0 = R2.bits[rc->vb + j];
                if (j > 0 && ((b0 & 0xf000000ull) == 0x4000000ull || (b0 & 0xf000000ull) == 0x5000000ull) && j + 1 < m) {
                    double c[2] = { WV_PT(j, 0), WV_PT(j, 1) }, s[2] = { WV_PT(j - 1, 0), WV_PT(j - 1, 1) }, e[2] = { WV_PT(j + 1, 0), WV_PT(j + 1, 1) };
                    int dir = (b0 & 0xf000000ull) == 0x4000000ull;              /* curve_rec: the direction byte set -> 0x4000000 */
                    double rr = hypot(s[0] - c[0], s[1] - c[1]), sw = hv_arc_sweep(c, rr, s, e, dir);
                    uint64_t be = R2.bits[rc->vb + j + 1] & 0xfffffffff0000000ull;
                    int ns = rr > chord_tol ? (int)ceil(sw / (2.0 * acos(fmax(-1.0, 1.0 - chord_tol / rr)))) : 1; if (ns < 1) ns = 1; if (ns > 4096) ns = 4096;
                    for (int t = 1; t < ns; t++) { double o2[2]; hv_arc_point(o2, c, rr, s, e, dir, (double)t / ns); WV_ADD(o2[0], o2[1], be != 0x40000000ull); }
                    continue;                                                  /* the arc's end vertex follows */
                }
                if (j == m - 1 && rc->closed) { if (q) sp[0] = (char)(bt != 0x40000000ull); break; }   /* the closing vertex: the span into the first point */
                WV_ADD(WV_PT(j, 0), WV_PT(j, 1), j > 0 && bt != 0x40000000ull);
            }
            #undef WV_ADD
            #undef WV_PT
            if (q >= 2) wlvor_push(out, xy, sp, q, rc->closed); else { free(xy); free(sp); }
        }
        hpt_passes_free(&R2);
    }
    hv_free(g); free(vv); free(pp);
    return ok;
}
static int gen_waterline(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    wl_arctan = 0;
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "Waterline here needs the Top tool plane at the origin"); return 0; }
    uint32_t rn = 0; uint8_t *rec = hst_record(f, p, NULL, &rn);
    if (!rec || rn < 0x1760) { free(rec); snprintf(op->error, sizeof op->error, "no Waterline record (Mastercam's default op 132 style 2 is missing)"); return 0; }
    #define RD(o) c3d(rec, rn, o)
    #define RB(o) c3b(rec, rn, o)
    /* refusals (wl 1 / 5): what is NOT DECODED is not generated */
    const char *why = NULL;
    double alo = RD(0x1248), ahi = RD(0x1250);
    if (RB(0x16c2)) why = "Add cuts (AdaptiveStepdown): NOT PORTED";
    else if (RB(0x16c1)) why = "Cut order Bottom to top (K+0x49a, LinkHeadNorthwards = false: FUN_180075b48): the linker is NOT DECODED";
    else if (RB(0x1730)) why = HST_CONTACT_WHY;
    /* contact angle limits other than 0..90: refused by user decision N9-2 (2026-11-09) until the filter's inferred inputs were decoded; decoded 2026-11-12
       (formats/hst3d_port_plan.md 7 "Contact angle filter inputs"): EvaWaterlinePassesPlan's constructor FUN_18040ea00 -> FUN_1803f9c20 (EvaZProfilePlan) ->
       FUN_1802eb170 (EvaMachiningBasePlan) -> FUN_1802f2340 sets the window; DAT_1809bf1a0 = acos(-1) x 0.5 = pi / 2 (mchmm.dll initializer at 180002320);
       PassesJoinGaps 1.0 is McHmm's mm (mckernel scales every length by job +0x80 = 25.4 for an inch part, FUN_18007bb48 mk l.91625-91633). */
    if (why) { snprintf(op->error, sizeof op->error, "Waterline: %s", why); free(rec); return 0; }
    /* the values (wl 1: FUN_180071314 style 1 mk l.87822-87911 and the common part) */
    int metric = mcam_is_metric(f);
    double D = p->tool_dia > 0 ? p->tool_dia : RD(0x350), R = D * 0.5;
    double Rc = fmin(fmax(p->tool_dia > 0 ? p->corner_r : RD(0x358), 0.0), R);
    double sd = RD(0x1228); if (!(sd > 1e-12)) sd = metric ? 2.5 : 0.1;          /* Stepdown TP+0x1b0 (l.87353-87364) */
    double sxy0 = RD(0x13c0), sz = RD(0x13c8), sxy = fmax(sxy0, sz);               /* ToolThicknessXY = max(XY, Z stock) (l.87577-87611) */
    double ztol = RD(0x12a0), tol = c3_step_tol(rec, rn, metric);                   /* StepMachTolerance (FUN_18006dd68, c3_step_tol) */
    int method = RB(0x16bd) ? 2 : RB(0x16be) == 0 ? 1 : 0;                          /* 0 Climb, 1 Conventional, 2 Zigzag (page 1324 combo 1239) */
    /* Use Z depths (TP+0x464 = record 0x16c9 [inferred], page 1325): PosZHi / PosZLo = max / min of TP+0x1c0 / 0x1c8 (record 0x1238 / 0x1240)
       + job +0x28 (the tool plane origin's Z: 0 here, Top at the origin) - FUN_180071314 l.87321-87344 */
    int zdep = RB(0x16c9); double zd1 = RD(0x1238), zd2 = RD(0x1240);
    /* check surfaces: Edit Passes to Surfaces 0x141f after the Waterline Passes Plan (mk l.93590-93603; cam3d_checktrim.c) */
    char cerr[200]; C3Chk *chk = c3_chk_open(f, op, rec, rn, ztol > 1e-9 ? ztol : tol * 0.1, R, Rc, metric, cerr, sizeof cerr);
    if (!chk && cerr[0]) { snprintf(op->error, sizeof op->error, "Waterline: %s", cerr); free(rec); return 0; }
    int pco = RB(0x16cf) != 0;                                                      /* PosContactOnly: false only when Flute contact is off (mk l.87892-87896; FUN_180303660 default true) */
    int smooth_rq = RB(0x16c3) != 0; double wl_ctol = RD(0x1270), wl_cmaxr = RD(0x1278);   /* ProfileSmoothing K+0x461, its CornerTolerance / MaxRadius TP+0x1f0 / +0x1f8 (tools/hmm_macro.py) */
    int wl_k472 = RB(0x16d7) != 0;                                                  /* K+0x472 (record 0x16d7) */
    /* the Waterline Linking Plan's order (above gen_waterline) applies when the decoded case holds: no check trimming (the 0x141f / 0x1458 frags' graph is
       NOT DECODED), LinkByZLevel false (record 0x16c0 != 0; true -> FragOrderer's FUN_180170520 key, NOT DECODED), LinkWaterlineDirectionStyle 0 (Zigzag
       0x16bd off, 0x16be on: FUN_180075b48 mk l.90015-90037; styles 1 / 2 not checked with the DLL), no start point (0x16bf: LinkStartX / Y = -9999,
       mcmsurf FUN_1800199d0), LinkSafeZ known (clearance 0x179 on and absolute 0x178 = 0, mk l.89177-89205) - else the old order (ours) */
    double u0 = metric ? 1.0 : 25.4;
    int wlo = !chk && RB(0x16c0) != 0 && !RB(0x16bd) && RB(0x16be) && !RB(0x16bf) && RB(0x179) && !RB(0x178);
    double wl_safez = (RD(0x170) - (RB(0x2f8) ? 0.0 : Rc)) * u0;   /* LinkSafeZ (plane origin Z 0: Top at the origin) */
    double wl_maxstay = RB(0x16cd) == 1 ? D * u0 * RD(0x1310) / 100.0 : RB(0x16cd) == 0 ? RD(0x1308) * u0 : (metric ? 2.5 : 0.15 * 25.4);   /* LinkMaxStayOnSurface (mk l.89205-89235) */
    int wl_tr = RB(0x16d2); double wl_ramp = fmax(0.01, RD(0x1358) * 180.0 / M_PI);   /* Transitions: 2 Straight = SurfaceRampStyle 0; SurfaceRampAngle (mk l.90090-90128) */
    /* the retract's leads (wl_retract; FUN_180075b48's Waterline block mk l.90129-90168, tools/hmm_macro.py link_params; the plan fields hst_linker.md 3):
       HorizontalLeadIn / OutRadius 0x1368 (when >= 0; else the plan default HelixDiameter / 2: not ported -> SAFE) / 0x1370, LinkVerticalLeadIn / Out
       0x1348 / 0x1350, LinkRampHeightOffset = the feed plane 0x190, MaxRampAngle 0x1378 (radians; 3 deg when not above the tolerance), RampHeightOffset
       0x1420 (the plan keeps a float, +0x11a4), plan +0x1240 = max(Stepdown, MachStepover) (hl l.4892-4899; MachStepover 0x1ad9 = 0 for Waterline:
       traced on op 4), LiftAngle 0 (the plan default: not written), LinkRetractStyle 2 (0x16c4; +0x1108 / +0x1109), LinkZClearance (wl_safez) */
    WlLead wlead; memset(&wlead, 0, sizeof wlead);
    wlead.rin = RD(0x1368) * u0; wlead.rout = RD(0x1370) * u0; wlead.rvin = RD(0x1348) * u0; wlead.rvout = RD(0x1350) * u0; wlead.off = RD(0x190) * u0;
    wlead.ain = RD(0x1378) > 0.0 ? RD(0x1378) : 3.0 * M_PI / 180.0; wlead.aout = 0.0; wlead.rho = (float)(RD(0x1420) * u0);
    wlead.sdown = fmax(sd * u0, 0.0); wlead.clr = wl_safez; wlead.atol = 0.45 * RD(0x12a0) * u0; wlead.dbg = getenv("MCAM_WL_LEADDBG") != NULL;
    /* LinkTrimmingStyle / LinkMaxAllowedTrimming: mckernel's linking block (mk l.89237-89272, the same block as Raster's: 0x1b1c = 0 when min(the
       vertical lead radii) <= the tolerance - taken as 0 as tools/hmm_macro.py does - else record 0x16d1; record 0x13a0) */
    wlead.tstyle = fmin(wlead.rvin, wlead.rvout) <= 0.0 ? 0 : RB(0x16d1); wlead.maxtrim = RD(0x13a0) * u0;
    wlead.on = RB(0x16c4) == 2 && wlead.rin > 0.0 && wlead.rout > 0.0 && RD(0x12a0) > 0.0 && !getenv("MCAM_WL_NOLEADS");   /* MCAM_WL_NOLEADS (testing): SAFE as before */
    free(rec);
    #undef RD
    #undef RB
    if (!(D > 0)) { snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); c3_chk_close(chk); return 0; }
    C3Mesh M; char err[150];
    if (!c3_mesh_op(f, op, ztol > 1e-9 ? ztol : tol * 0.1, &M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); c3_chk_close(chk); return 0; }
    /* PosZHi / PosZLo (FUN_18007bb48, hst3d_port_plan.md 6.7): the drive's Z range, floor stock and the tolerance; style 1 is in the mask 0x5b6:
       PosZLo also lowered by the corner radius */
    if (fmin(sxy, sz) + Rc < -ztol) { snprintf(op->error, sizeof op->error, "negative stock to leave below the corner radius: NOT DECODED"); c3_mesh_free(&M); c3_chk_close(chk); return 0; }
    if (getenv("MCAM_HMM_DUMP")) hmm_dump(f, p, &M, metric, D, Rc, chk);
    double zhi = M.hi[2], zlo = M.lo[2];
    if (sz > 0) zhi += sz; else if (sz < 0) zlo += sz;
    zhi += ztol; zlo -= ztol; zlo -= Rc;
    if (zdep) { zhi = fmax(zd1, zd2); zlo = fmin(zd1, zd2); }
    /* the weave shape as OptiRough's (plan 4.5) */
    int flat_tip = Rc + sz <= 0.0;
    double Rb = flat_tip ? 0.0 : Rc + sz, Fo = flat_tip ? R + sxy : (R - Rc) + sxy - sz;
    double spacing = c3_resolution(R, Rc, sz, tol); if (!(spacing > 0)) spacing = R;
    double arc_tol = fmax(tol * 0.5, 1e-4);
    /* ContactAngleLo / Hi (mk l.87268-87304: min / max of 0x1248 / 0x1250, each clamped to [0, 0x3ff921fb54442d15], x 57.29577951308233 = degrees) and the cosine
       window of FUN_18040b6f0 (above wl_contact_nz). mckernel hands the plan the degrees as macro text "%19.9f" (FUN_1800edd8c, mk l.183616): the record's 90 deg is
       1.570796326794896 rad = 89.99999999999997 deg, written "90.000000000" - so the plan sees 90 and 50 exactly (formats/hst_linker.md 31.2; without the text round
       trip Hi < 90 made ContactAngleTol ~3e-15 deg and the window's low end cos(Hi) > 0, dropping every vertical-wall contact, nz = 0). The plan reads them back in
       radians, deg x DAT_1809bf198 / 360 (FUN_180389560; DAT_1809bf198 = 2 acos(-1)), and compares with DAT_1809bf1a0 = acos(-1) x 0.5 (hst3d_port_plan.md 7) */
    double hp_c; { uint64_t b = 0x3ff921fb54442d15ull; memcpy(&hp_c, &b, 8); }
    double alo_m = fmin(alo, ahi), ahi_m = fmax(alo, ahi);
    alo_m = alo_m > hp_c ? hp_c : (alo_m < 0 ? 0.0 : alo_m); ahi_m = ahi_m > hp_c ? hp_c : (ahi_m < 0 ? 0.0 : ahi_m);
    double lod, hid; { char t[64]; snprintf(t, sizeof t, "%19.9f", alo_m * 57.29577951308233); lod = strtod(t, NULL); snprintf(t, sizeof t, "%19.9f", ahi_m * 57.29577951308233); hid = strtod(t, NULL); }
    double tpi = acos(-1.0) * 2, hpi = acos(-1.0) * 0.5, alo_c = lod * tpi / 360.0, ahi_c = hid * tpi / 360.0;
    double tol_c = fmin(lod > 0 ? fmin(lod / 10.0, 1.0) : 1.0, hid < 90 ? fmin((90 - hid) / 10.0, 1.0) : 1.0) * tpi / 360.0;
    double cw[4] = { ahi_c <= hpi - tol_c ? cos(ahi_c) : -0.1, tol_c <= alo_c ? cos(alo_c) : 1.1, ahi_c <= hpi - tol_c ? cos(ahi_c + tol_c) : -0.1, tol_c <= alo_c ? cos(alo_c - tol_c) : 1.1 };
    if (getenv("MCAM_WL_CTDUMP")) fprintf(stderr, "wlwin %.17g %.17g %.17g %.17g\n", cw[0], cw[1], cw[2], cw[3]);   /* testing: = the DLL's Eva +0x678..+0x690 */
    int lim = cw[0] > -0.05 || cw[1] < 1.05;                                       /* the default 0..90 leaves the whole range [-0.1, 1.1] */
    double joingap = metric ? 1.0 : 1.0 / 25.4;                                     /* PassesJoinGaps 0x1ad6: 1.0 mm */
    WlTg tg; memset(&tg, 0, sizeof tg);
    if (lim) {
        if (flat_tip) { snprintf(op->error, sizeof op->error, "Waterline: contact angle limits with a flat tool: the contact normal at the tool's edge is NOT DECODED"); c3_mesh_free(&M); c3_chk_close(chk); return 0; }
        wltg_build(&tg, &M, fmax((Fo + 1.25 * Rb) * 0.5, 1e-6));
    }
    /* the containment: the op's chains as tmptc.tmp holds them (c3_containment), else the Bounding Box Plan (below) */
    CPaths region = { 0 }; int boxcont = 0;
    { char ce[150]; if (!c3_containment(f, op, c3_hst_tcb_type(op), &region, ce, sizeof ce)) { snprintf(op->error, sizeof op->error, "%s", ce); if (lim) wltg_free(&tg); c3_mesh_free(&M); c3_chk_close(chk); return 0; } }
    { uint32_t rn2 = 0; uint8_t *r2 = hst_record(f, p, NULL, &rn2); if (r2) { int ok = c3_tcb_offset(&region, r2, rn2, D, fmax(tol * 0.5, 1e-4)); free(r2); if (!ok) { snprintf(op->error, sizeof op->error, C3_TCB_FAIL); cl_free(&region); c3_mesh_free(&M); c3_chk_close(chk); return 0; } } }   /* the containment as written to tmptc.tmp (c3_tcb_offset) */
    if (getenv("MCAM_HMM_DUMP")) hmm_dump_tc(&region);
    /* no containment: the Bounding Box Plan 0x1425 of the surfaces, OffsetDistance = |0x13c0| + FUN_18006fee0 * 0.5 + 2 x 0x12a0 (mk l.92848-92875, the
       styles not in the mask 0x209; FUN_18006fee0 = the diameter for an untapered tool) */
    if (!region.n) { double w = fabs(sxy0) + R + 2 * ztol; region.p = calloc(1, sizeof(CPath)); region.p[0] = c3_box_path(M.lo[0] - w, M.lo[1] - w, M.hi[0] + w, M.hi[1] + w); region.n = 1; boxcont = 1; }
    double bx[4] = { 1e300, 1e300, -1e300, -1e300 };
    for (int i = 0; i < region.n; i++) for (int k = 0; k < region.p[i].n; k++) { bx[0] = fmin(bx[0], region.p[i].xy[2 * k]); bx[1] = fmin(bx[1], region.p[i].xy[2 * k + 1]); bx[2] = fmax(bx[2], region.p[i].xy[2 * k]); bx[3] = fmax(bx[3], region.p[i].xy[2 * k + 1]); }
    /* the slice weave's box and its boundary fill (hst3d_port_plan.md 7 "The weave box and the boundary fill"; ball / bull tools - a flat tip has no fibres here, its slice
       is the projected triangles, kept inside the containment by wl_inside below): BoundaryOutwardsOffsetRad 0x1bf0 default "VorProfileFlatRad + (PosContactOnly ? 1.0 +
       MachWeaveResolution : 0.0)" (FUN_180310790 hp l.1127-1137; McHmm mm), not written by mckernel. FUN_1803fb4f0 (hz l.1150-1165) multiplies it by 1.1 when PosContactOnly
       and it equals the first field of the tool profile Eva +0x208 shifted by -VorProfileFlatRad (FUN_18013e330(Eva +0x208, 0, 0, -VorProfileFlatRad)). Eva +0x208 is built by
       FUN_1802f5270 (from EvaMachLocBasePlan's constructor FUN_1802f2340): FUN_18013e200(ToolShaftRadius 0x1abe, max(ToolCornerRadius 0x1abd, -ToolThicknessZ)) =
       { [0] shaft radius, [1] corner radius, [3] = [0] - [1] }, then FUN_18013e330(that, ToolHolderThickness, ToolThicknessZ, 0) adds ToolThicknessZ to [0] and [1]. The shift
       by -VorProfileFlatRad: when that equals -[3] (a ball / bull: VorProfileFlatRad = shaft - corner) [0] = [1] = corner + ToolThicknessZ; else (a flat tip, -0.0) [0] =
       shaft + ToolThicknessZ. The comparison is exact, in McHmm's mm (mckernel scales lengths by job +0x80). The fibre ranges = the boundary collection's box grown by
       MachWeaveResolution + that + 1.0 (+ the stock-shaft term of CheckAreaClearanceStockShaft 0x1cc0, Eva +0x1030: off for Waterline) - FUN_180270d10 -> FUN_18012bc80 ->
       FUN_18012be70 (wd l.18891); FUN_18012bf60 fills the outside of the boundaries grown by it (wv_fill). */
    double mm = metric ? 1.0 : 1.0 / 25.4, u = metric ? 1.0 : 25.4, vfr = flat_tip ? 0.0 : fmax(0.0, R - Rc), boor = vfr + (pco ? mm + spacing : 0.0);
    { double vfr_mm = flat_tip ? 0.0 : fmax(0.0, R * u - Rc * u), c1 = fmax(Rc * u, -sz * u), b3 = R * u - c1;
      double pf0 = vfr_mm > 0.0 && vfr_mm == b3 ? c1 + sz * u : R * u + sz * u - vfr_mm;   /* FUN_18013e330's two branches */
      double boor_mm = vfr_mm + (pco ? 1.0 + spacing * u : 0.0);
      if (pco && boor_mm == pf0) boor *= 1.1; }
    double wgrow = spacing + boor + mm;
    double wrng[4] = { bx[0] - wgrow, bx[1] - wgrow, bx[2] + wgrow, bx[3] + wgrow };
    WvFill wfill = { &region, boor, { bx[0], bx[1], bx[2], bx[3] } };
    /* the flat-radius offset (bull tools, or XY stock above Z stock): McHmm's chain on the weave's pockets (wlvor_pocket above); MCAM_WL_CLIPPER=1 (testing):
       the Clipper offset of the union clipped to the containment by wl_inside, the port before 2026-11-14 (ours) */
    int vor = !flat_tip && (fabs(Fo) > 1e-12 || getenv("MCAM_WL_VOR_ALL")) && !getenv("MCAM_WL_CLIPPER");   /* MCAM_WL_VOR_ALL (testing): ball tools too (r' = 0, as the DLL) */
    if (smooth_rq && !vor) {
        snprintf(op->error, sizeof op->error, "Waterline: Corner rounding (ProfileSmoothing) without a flat-radius offset: the Voronoi offset by r' = 0 is not wired (NOT PORTED)");
        cl_free(&region); c3_mesh_free(&M); if (lim) wltg_free(&tg); c3_chk_close(chk); return 0; }
    WlVor V; memset(&V, 0, sizeof V);
    if (vor) {
        double tol_mm = tol * u, d2r = acos(-1.0) * 2 / 360.0;              /* FUN_180389560: degrees x DAT_1809bf198 / 360 */
        /* the filter writer's template (FUN_1803faee0 hz l.960-1000): mode 1, VoronoiInputCurveTolerance 0x1bc3 "max(0.0001, (min(0.002, 0.25 * <0x1acc>)
           / 3.0))", cos(VoronoiInputCurveAngleTolerance 0x1bc4 = 85 deg), VoronoiInputCurveMinSegmentLength 0x1bc5 = 0.001, 0, -1, no straight lines,
           cos / sin(0.0001) (hp l.836-900 the defaults; not written by mckernel) */
        V.t1 = (HptTemplate){ 1, fmax(0.0001, fmin(0.002, 0.25 * tol_mm) / 3.0), cos(85.0 * d2r), 0.001, 0.0, -1.0, 0, cos(0.0001), sin(0.0001) };
        V.steptol = tol_mm;                                                   /* StepMachTolerance (FUN_1803faee0 reads 0x1acc, hz l.964-967) */
        /* Eva +0x4a8 (FUN_1802f2340 dis 1802f2402-1802f278f): mode 2 for the Waterline Passes Plan (ThinArcEnable "true", plan 7 W7), ThinCurveTolerance
           "min(Tolerance 0x1a9d, StepMachTolerance)" (both job +0xa0 from mckernel), cos(ThinAngleTolerance 0x1ad3 = 45), ThinSegmentLengthMin 0x1ad2 = 0.0005,
           ThinSegmentLengthMax 0x1ad1 = -1, ThinArcMaxRadius 0x1ec7 = -1, StraightLineEnable 0x1c7f "true", cos / sin(StraightLineAngleTolerance 0x1c7e = 0.1)
           (hp l.1594-1710; traced: the template on SWARF op 4) */
        V.t2 = (HptTemplate){ 2, tol_mm, cos(45.0 * d2r), 0.0005, -1.0, -1.0, 1, cos(0.1 * d2r), sin(0.1 * d2r) };
        /* Eva +0x15c0 = !WeaveSubdivision && PosContactOnly && ToolHolderDefinition not empty (hz l.1232-1250): mckernel writes ToolHolderDefinition
           (FUN_180079d34 mk l.90741-90770) only through K+0x472 (FUN_180079bfc) or a tool of type 0x84 with byte 6 - neither is modelled here, so the
           port requires K+0x472 off and takes the holder as empty (traced 0 on SWARF op 4) */
        if (wl_k472) { snprintf(op->error, sizeof op->error, "Waterline: K+0x472 (FUN_180079bfc: the tool holder definition for the angle filter's flag rule, Eva +0x15c0) is NOT DECODED");
            cl_free(&region); c3_mesh_free(&M); if (lim) wltg_free(&tg); c3_chk_close(chk); return 0; }
        /* WeaveSubdivision 0x1bc2 (Eva +0x1001) default "(VorProfileFlatRad < 1.5 * MachWeaveResolution)" (hp l.811-822; traced 1 on MOLD BOTTLE op 4, 0 on SWARF
           op 4): FUN_18040b6f0 then calls FUN_18040af10 (hz l.11453) on the spans it writes - NOT DECODED */
        if (fmax(0.0, R - Rc) < 1.5 * spacing) { snprintf(op->error, sizeof op->error, "Waterline: WeaveSubdivision (VorProfileFlatRad < 1.5 x MachWeaveResolution): the angle filter's span subdivision FUN_18040af10 is NOT DECODED");
            cl_free(&region); c3_mesh_free(&M); if (lim) wltg_free(&tg); c3_chk_close(chk); return 0; }
        V.e15c0 = 0; V.pco = pco;
        hv_params_default(&V.hp);
        V.r0 = fmax(0.0, R * u - Rc * u);                                     /* VorProfileFlatRad (plan 4.5; Eva +0xff8 traced 3.175 = 0.125 in) */
        if (fabs(V.r0 - Fo * u) > 1e-9) { snprintf(op->error, sizeof op->error, "Waterline: XY stock other than the Z stock with the Voronoi offset (Eva +0xff8 = VorProfileFlatRad vs ToolThicknessXY - ToolThicknessZ): NOT DECODED");
            cl_free(&region); c3_mesh_free(&M); if (lim) wltg_free(&tg); c3_chk_close(chk); return 0; }
        V.pushup = 0.0002; V.pushlim = 0.002;                                 /* RadPushUp / RadPushUpLimit as mckernel writes them (mk l.87822-87857, tools/hmm_macro.py) */
        V.mindev = 0.001; V.minrad = 0.0001; V.minchord = 0.00015;            /* ProfileSmoothingMinDeviation 0x1bd4 / MinRadius 0x1c00 / MinArcChordLength 0x1bd3 (hp l.960-1035; traced) */
        V.peps = 2e-5;                                                        /* VoronoiPolyEpsilon 0x1bce (hp l.944; traced copy +0x78) */
        /* ProfileSmoothing (record 0x16c3 = K+0x461): CornerTolerance = max(TP+0x1f0 = record 0x1270, 2 x job +0xa0), MaxRadius = TP+0x1f8 = record 0x1278
           (mk l.87279-87283); off: CornerTolerance 0 (FUN_180406b00 hp l.1811-2050) and MaxRadius the plan's "ToolCutterDiameterAdjusted * 0.05" (traced 0.635 mm) */
        if (smooth_rq) { V.tau = fmax(wl_ctol, 2.0 * tol) * u; V.maxrad = wl_cmaxr * u; } else { V.tau = 0.0; V.maxrad = D * u * 0.05; }
    }
    /* with a flat-radius offset (MCAM_WL_CLIPPER) the fill's outline becomes a loop at BoundaryOutwardsOffsetRad - Fo outside the containment, dropped by wl_inside only when that is > 0 */
    if (!vor && !flat_tip && fabs(Fo) > 1e-12 && !(boor - Fo > 1e-9)) {
        snprintf(op->error, sizeof op->error, "Waterline: NOT PORTED: the boundary fill's clipped passes need the mchmm Voronoi offset (hst3d_port_plan.md 7 stage 3)");
        cl_free(&region); c3_mesh_free(&M); if (lim) wltg_free(&tg); c3_chk_close(chk); return 0; }
    /* the XY box of the inflexion levels: CZInflexions (FUN_1804a1b80, hz l.12956) takes the weave's cull box +0x168..+0x180, which FUN_180271070 (hz l.12944, hmm-plans1
       l.2411) re-fits before every level: over the fibres with two or more intervals, the span from the first interval's hi to the last one's lo and the fibres' positions,
       grown by +0x2d8 (MachWeaveResolution, FUN_180270d10) and then by max(the tool's radius at the model's top above the level (FUN_18013fff0), the weave profile's first
       field). The weave then holds the template (the boundary fill only): FUN_18040e430 copies the template Eva +0x1090 back into it after each level (FUN_18012c0f0 hz
       l.14423). The weave profile here is the ball (corner + ToolThicknessZ); the side-profile table of FUN_18013fff0 above it (shank / holder) is not modelled, as in the
       slice. A flat tip (no fibres in our slice): the containment box (ours, as before). */
    double ibx[4] = { bx[0], bx[1], bx[2], bx[3] };
    if (!flat_tip) {
        double cb[4] = { 1e300, 1e300, -1e300, -1e300 }; int got = 0;
        for (int ax = 0; ax < 2; ax++) {                   /* ax 0: set +0x10 (fibres at x, along y); ax 1: set +0x58 (fibres at y, along x) */
            double lo = wrng[ax ? 1 : 0], hi = wrng[ax ? 3 : 2], elo = wrng[ax ? 0 : 1], ehi = wrng[ax ? 2 : 3];
            int n = (int)((hi - lo) / spacing) + 2; if (n < 3) n = 3; n++; if (n > 200000) n = 200000;   /* as c3_slice (FUN_180131690: max(3, ..) + 1) */
            for (int k = 0; k < n; k++) {
                WvFib fb = { 0 }; { double t = (double)k / (double)(n - 1); fb.pos = (1.0 - t) * lo + hi * t; }
                wv_fill(&wfill, &fb, ax, elo, ehi);
                if (fb.n >= 2) { got = 1;
                    cb[ax ? 0 : 1] = fmin(cb[ax ? 0 : 1], fb.v[0].hi); cb[ax ? 2 : 3] = fmax(cb[ax ? 2 : 3], fb.v[fb.n - 1].lo);
                    cb[ax ? 1 : 0] = fmin(cb[ax ? 1 : 0], fb.pos); cb[ax ? 3 : 2] = fmax(cb[ax ? 3 : 2], fb.pos); }
                free(fb.v);
            }
        }
        double g = got ? spacing + Rb : Rb;
        if (!got) memcpy(cb, wrng, sizeof cb);
        ibx[0] = cb[0] - g; ibx[1] = cb[1] - g; ibx[2] = cb[2] + g; ibx[3] = cb[3] + g;
    }
    /* the levels: Waterline's StepDownEvenLastSteps 5 (hp l.596-601) and CZInflexions with the default Avoidance (c3_levels_infl) */
    /* N13-30 (2026-11-14): CZInflexions with FUN_1804a2270's edges and vertices (FUN_1804a20b0 / FUN_1804a2040) as decoded - on our tessellation the
       levels move up by up to 0.009 mm where Mastercam's mesh has no vertex in the window (formats/hst_linker.md 21.1, 22.1) */
    double *zl = NULL; int nz = c3_levels_infl(&M, ibx, sz, zhi, zlo, sd, 5, -1, metric ? 1.0 : 1.0 / 25.4, &zl);
    if (nz < 0) { snprintf(op->error, sizeof op->error, nz == -1 ? "cancelled" : "the Z levels do not descend (the stepdown is below the numeric precision of the Z range)"); free(zl); cl_free(&region); c3_mesh_free(&M); if (lim) wltg_free(&tg); c3_chk_close(chk); return 0; }
    if (getenv("MCAM_WL_LEVELS")) {   /* testing (formats/hst_linker.md 31): the levels from a file (one Z per line, op units; e.g. the DLL's), to compare the weave vertex by vertex */
        FILE *lf = fopen(getenv("MCAM_WL_LEVELS"), "r"); double v; nz = 0;
        if (lf) { while (fscanf(lf, "%lf", &v) == 1) { zl = realloc(zl, sizeof(double) * (nz + 1)); zl[nz++] = v; } fclose(lf); } }
    if (getenv("MCAM_3D_LEVELS")) { for (int k = 0; k < nz; k++) fprintf(stderr, "level %.4f\n", zl[k]); }   /* ours (testing) */
    double fp = p->top_z + p->feed_plane; if (fp < zhi + p->feed_plane) fp = zhi + p->feed_plane;
    double feed = p->feed > 0 ? p->feed : 100, plunge = p->plunge_feed > 0 ? p->plunge_feed : feed;
    int okall = 1, nloops = 0, down = 0; double cx = bx[0], cy = bx[1];
    /* the linker's order (wlo): the passes (mm, N13-27) with their regions are collected first; wl_fail = a step NOT DECODED was met -> the old order */
    WlPass *W = NULL; int nw = 0, capw = 0; WlNode *N = NULL; int nn = 0, capn = 0, wl_fail = 0;
    /* MCAM_WL_PASSDUMP=FILE (testing): the passes as they are made, before the linker or the check trimming ("PASS n" + "x y z", mm, as tools/hmm_oracle.py's .mmpasses) */
    FILE *wl_pd = getenv("MCAM_WL_PASSDUMP") ? fopen(getenv("MCAM_WL_PASSDUMP"), "w") : NULL;
    CPaths wl_prev = { 0 }; int *wl_prev_node = NULL, *wl_prev_dep = NULL;
    for (int k = 0; k < nz && okall; k++) {
        double z = zl[k];
        if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); okall = 0; break; }
        /* the weave's loops by McHmm's traversal (cam3d_rough.c wv_loops); WeaveResolveAmbiguous 0x1bef default "(VorProfileFlatRad < 1.5 * MachWeaveResolution)"
           (FUN_180310790 hp l.1112-1120; VorProfileFlatRad = max(0, ToolShaftRadius - ToolCornerRadius), 0 for a flat tip: plan 4.5), not written by mckernel */
        c3_weave_mc = 1; c3_weave_resolve = (flat_tip ? 0.0 : fmax(0.0, R - Rc)) < 1.5 * spacing;
        /* the weave's box (above): ball / bull tools the fibre ranges wrng with the boundary fill; a flat tip (no fibres) the drive's XY box */
        double wbx[4] = { M.lo[0], M.lo[1], M.hi[0], M.hi[1] };
        CPaths wraw = { 0 }; unsigned **wfl = NULL; WvPockets wpk; memset(&wpk, 0, sizeof wpk);
        if (!flat_tip) { c3_weave_rng = wrng; c3_weave_fill = &wfill; c3_weave_raw = &wraw; c3_weave_rawfl = &wfl; if (vor) c3_weave_pk = &wpk; }
        if (getenv("MCAM_WL_FIBDUMP")) fprintf(stderr, "wlfib level %.9f\n", z * u);   /* testing (cam3d_rough.c wv_loops) */
        CPaths forb = c3_slice(&M, z - sz, Rb, z + Rc, vor ? 0.0 : Fo, spacing, wbx, arc_tol);
        c3_weave_rng = NULL; c3_weave_fill = NULL; c3_weave_raw = NULL; c3_weave_rawfl = NULL; c3_weave_pk = NULL;
        WlVorOut vo; memset(&vo, 0, sizeof vo);
        if (vor && !c3_weave_err) {   /* the angle filter on each pocket's loops (FUN_18040b6f0), then McHmm's offset chain (wlvor_pocket) */
            char verr[300] = ""; int vok = 1;
            for (int a = 0; a < wpk.npk && vok; ) {
                int e = a; while (e < wpk.npk && wpk.pk[e] >= 0) e++;
                int nlp = e - a; const CPath **lp = malloc(sizeof(CPath *) * (nlp ? nlp : 1)); char **kp = malloc(sizeof(char *) * (nlp ? nlp : 1));
                for (int t = 0; t < nlp; t++) { const CPath *L = &wpk.L.p[wpk.pk[a + t]]; lp[t] = L; kp[t] = malloc(L->n ? L->n : 1);
                    if (L->n >= 3) wl_angle_keep(&tg, L, z + Rc, 0.0, Rb, cw, joingap, wpk.fl[wpk.pk[a + t]], pco ? 1 : -1, 1, lim, kp[t]);
                    else for (int q = 0; q < L->n; q++) kp[t][q] = !(wpk.fl[wpk.pk[a + t]][q] & 0x80000000u);   /* a ring of one or two nodes: the clipped test only (NOT CHECKED with the DLL) */
                    if (getenv("MCAM_WL_VORDUMP")) { fprintf(stderr, "wlvor loop z %.9f pocket %d loop %d n %d\n", z * u, a, t, L->n);   /* testing */
                        for (int q = 0; q < L->n; q++) fprintf(stderr, "wlvor v %.17g %.17g %08x %d\n", L->xy[2 * q] * u, L->xy[2 * q + 1] * u, wpk.fl[wpk.pk[a + t]][q], kp[t][q]); } }
                /* the loops in mm (McHmm's unit) */
                CPath *lm = malloc(sizeof(CPath) * (nlp ? nlp : 1)); const CPath **lpm = malloc(sizeof(CPath *) * (nlp ? nlp : 1));
                for (int t = 0; t < nlp; t++) { lm[t].n = lp[t]->n; lm[t].xy = malloc(sizeof(double) * 2 * (lm[t].n ? lm[t].n : 1)); for (int q = 0; q < 2 * lm[t].n; q++) lm[t].xy[q] = lp[t]->xy[q] * u; lpm[t] = &lm[t]; }
                vok = wlvor_pocket(&V, lpm, kp, nlp, z * u, u, arc_tol * u, &vo, verr, sizeof verr);
                for (int t = 0; t < nlp; t++) { free(kp[t]); free(lm[t].xy); }
                free(lp); free(kp); free(lm); free(lpm);
                a = e + 1;
            }
            if (!vok) { snprintf(op->error, sizeof op->error, "Waterline at Z %.4f: %s", z, verr); okall = 0; }
            if (getenv("MCAM_WL_VORDUMP")) for (int i = 0; i < vo.L.n; i++) { fprintf(stderr, "wlvor pass z %.9f n %d closed %d\n", z * u, vo.L.p[i].n, vo.closed[i]);   /* testing */
                for (int q = 0; q < vo.L.p[i].n; q++) fprintf(stderr, "wlvor p %.17g %.17g %d\n", vo.L.p[i].xy[2 * q] * u, vo.L.p[i].xy[2 * q + 1] * u, vo.sp[i][q]); }
        }
        wv_pockets_free(&wpk);
        if (!okall) { cl_free(&forb); for (int i_ = 0; i_ < wraw.n; i_++) free(wfl[i_]); free(wfl); cl_free(&wraw); wlvor_out_free(&vo); break; }
        /* direct: no flat-radius offset (Fo = 0), so the passes are the weave's own loops with their clipped flags (FUN_18040b6f0 on the loop vertices; the
           Voronoi offset at t = 0 by a zero radius leaves them - ours: the DLL also thins and arc-fits them there, not wired for Fo = 0); vor: McHmm's chain
           (wlvor_pocket, the passes with their span flags); MCAM_WL_CLIPPER (testing): our Clipper-offset polygons clipped to the containment (wl_inside) */
        int direct = !flat_tip && fabs(Fo) <= 1e-12;
        if (direct && wraw.n && cl_area(&wraw.p[wraw.n - 1]) < 0)   /* the grid outline (the last loop, c3_slice) runs as Clipper's outer loops do: CCW */
            for (int i = 0; i < wraw.n; i++) { CPath *P = &wraw.p[i]; int n = P->n; reverse_path(P); unsigned *g = wfl[i]; for (int a = 0, c = n - 1; a < c; a++, c--) { unsigned t = g[a]; g[a] = g[c]; g[c] = t; } }
        CPaths *PL = vor ? &vo.L : direct ? &wraw : &forb;   /* the weave Z = level - c, c = Eva +0x140 = ToolThicknessZ (FUN_1802f4c80): FUN_18040cf60 hz l.13009, FUN_180413d90 hz l.16662, set at CWeave +0x158 by FUN_1801298a0; a triangle counts when h = zmax - z_weave > 0 (FUN_1801295b0) */
        c3_weave_mc = 0;
        #define WL_RAWFREE() do { for (int i_ = 0; i_ < wraw.n; i_++) free(wfl[i_]); free(wfl); cl_free(&wraw); } while (0)
        if (c3_weave_err) { snprintf(op->error, sizeof op->error, "Waterline at Z %.4f: %s", z, c3_weave_err); c3_weave_err = NULL; cl_free(&forb); WL_RAWFREE(); wlvor_out_free(&vo); okall = 0; break; }
        if (getenv("MCAM_WL_CTDUMP") && !direct && wfl) for (int i = 0; i < wraw.n; i++) if (wraw.p[i].n >= 3) {   /* testing: the filter on the weave's own loops (McHmm's input), dump only */
            fprintf(stderr, "wlweave\n"); char *dk = malloc(wraw.p[i].n); wl_angle_keep(&tg, &wraw.p[i], z + Rc, 0.0, Rb, cw, joingap, wfl[i], pco ? 1 : -1, 1, lim, dk); free(dk); fprintf(stderr, "wlweave end\n"); }
        if (!PL->n) { cl_free(&forb); WL_RAWFREE(); wlvor_out_free(&vo); continue; }
        (void)boxcont;
        /* the passes: the contour of the tool-centre region = the forbidden region's loops without their clipped vertices (the boundary fill's: the containment's
           own outline is not a pass, and a loop running into the fill splits into open passes - FUN_18040b6f0); a flat tip: the parts inside the containment (ours) */
        /* ours (the order within a level is Clipper's): the Waterline Linking Plan 0x141c decides order and direction, from LinkByZLevel = (0x16c0 == 0) and LinkWaterlineDirectionStyle
           (K+0x45d Zigzag -> 2, else K+0x45e == 0 -> 1, else 0: FUN_180075b48 mk l.90215-90240); the linker is NOT DECODED (formats/hst_linker.md 4: the order needs the adjacency graph FUN_180183a50 and FragOrderer's
           LinkByZLevel key FUN_180170520, not decoded), so the levels run in depth order whatever 0x16c0 says. */
        int rev = method == 1 || (method == 2 && (k & 1));   /* Conventional: reversed; Zigzag: bidirectional (here level by level, ours as above) */
        /* the regions of this level and their parents (FUN_180400db0 / FUN_1803fe090, above gen_waterline) */
        int *lnode = NULL, *hnode = NULL, *ldep = NULL;
        if (wlo && !wl_fail) {
            lnode = malloc(sizeof(int) * PL->n); hnode = malloc(sizeof(int) * (PL->n + 1)); ldep = malloc(sizeof(int) * PL->n);
            for (int i = 0; i <= PL->n; i++) hnode[i] = -1;
            wl_depths(PL, ldep);
            for (int i = 0; i < PL->n && !wl_fail; i++) {
                lnode[i] = -1; if (PL->p[i].n < 3) continue;
                double q[2]; int h;
                if (!wl_probe(PL, i, 0, ldep, mm, q, &h)) { wl_fail = 1; break; }   /* the loop as stored: the forbidden region on its left, so the right-hand normal points into the free region (the region on the right, as the region curves of FUN_1803fedb0); its fallback: NOT DECODED */
                if (hnode[h + 1] < 0) {
                    if (nn == capn) { capn = capn ? 2 * capn : 64; N = realloc(N, sizeof(WlNode) * capn); }
                    N[nn].level = k; N[nn].h = h; N[nn].npar = 0; N[nn].par = NULL; N[nn].z = z * u; hnode[h + 1] = nn++; }
                int me = lnode[i] = hnode[h + 1];
                if (getenv("MCAM_WL_ORDER")) fprintf(stderr, "wl region: level %d z %.6f loop %d/%d n %d depth %d probe %.6f %.6f h %d node %d area %.6f\n", k, z, i, PL->n, PL->p[i].n, ldep[i], q[0], q[1], h, me, cl_area(&PL->p[i]));   /* testing */
                if (wl_prev.n) {                                                 /* FUN_1803ff7b0: the previous level's region holding the probe */
                    int hp = wl_inner(&wl_prev, q[0], q[1], wl_prev_dep), pn = wl_prev_node[hp + 1], dup = 0;
                    for (int t = 0; t < N[me].npar; t++) if (N[me].par[t] == pn) dup = 1;
                    if (pn >= 0 && !dup) { N[me].par = realloc(N[me].par, sizeof(int) * (N[me].npar + 1)); N[me].par[N[me].npar++] = pn; }
                    if (getenv("MCAM_WL_ORDER")) fprintf(stderr, "wl parent: node %d hp %d pn %d\n", me, hp, pn);   /* testing */
                }
            }
        }
        for (int i = 0; i < PL->n; i++) {
            CPath *L = &PL->p[i]; int n = L->n; if (n < 3) continue;
            /* the forbidden region's loops run the other way round from the free region's: the material is on the left of a Clipper loop of
               it, so Climb (material on the right) runs them reversed */
            int r = !rev;
            char *in = malloc(n), *lk = NULL; unsigned *ck = direct && !vor ? wfl[i] : NULL; int nin = 0, nreg = 0, s0 = 0;
            const char *vsp = vor ? vo.sp[i] : NULL;                             /* the Voronoi passes: span flags (wlvor_pocket) */
            if (vsp) {   /* a closed record: nin = its kept spans; an open record runs from its first point (reversed: from its last) */
                nreg = n; for (int q = 0; q < n; q++) { in[q] = vsp[q]; nin += vo.closed[i] ? vsp[q] : q > 0 && vsp[q]; }
                if (!vo.closed[i]) { nin = -1; s0 = r ? n - 1 : 0; }
                else if (nin == n) s0 = 0;                                     /* a whole record starts at its first point, the start FUN_180557f60 wrote (checked: SWARF op 4) */
                else for (int q = 0; q < n; q++) if (!vsp[r ? (q + 1) % n : q]) { s0 = q; break; }   /* the span into s0 along the run direction is cut */
                if (!nin) { free(in); continue; }
            } else {
            if (lim || ck) { lk = malloc(n); wl_angle_keep(&tg, L, z + Rc, Fo, Rb, cw, joingap, ck, pco ? 1 : -1, 1, lim, lk); }   /* the vertex filter FUN_18040b6f0 */
            /* the loop's points inside the region: all -> the loop is a closed item */
            for (int q = 0; q < n; q++) { int ir = direct ? 1 : wl_inside(&region, L->xy[2 * q], L->xy[2 * q + 1]); nreg += ir; in[q] = (char)ir && (!lk || lk[q]); nin += in[q]; }
            free(lk);
            if (!nin) { free(in); continue; }
            /* runs of inside points along the loop direction; a fully inside loop is one closed run starting at the point nearest the tool */
            if (nin == n) { double bd = 1e300; for (int q = 0; q < n; q++) { double d = hypot(L->xy[2 * q] - cx, L->xy[2 * q + 1] - cy); if (d < bd) { bd = d; s0 = q; } } }
            else { for (int q = 0; q < n; q++) if (!in[q]) { s0 = q; break; } }
            }
            #define PT(j) (r ? ((s0 - (j)) % n + n) % n : (s0 + (j)) % n)
            /* the span from PT(j) to PT(j + 1) of a Voronoi pass is kept: its flag sits on its end in the record's direction */
            #define SPK(j) (r ? vsp[PT(j)] : vsp[PT((j) + 1)])
            int j = 0, total = nin == n ? n + 1 : n;
            if (vsp && nin != n) total = vo.closed[i] ? n : n - 1;              /* spans: a closed record n (the last one cut), an open one n - 1 */
            while (j < total) {
                int a;
                if (vsp && nin != n) {   /* runs of kept spans: vertices a .. j (j - a + 1 of them) */
                    while (j < total && !SPK(j)) j++;
                    if (j >= total) break;
                    a = j; while (j < total && SPK(j)) j++;
                    j++;                                                         /* past the run's last vertex */
                } else {
                while (j < total && !in[PT(j)]) j++;
                if (j >= total) break;
                a = j; while (j < total && in[PT(j)]) j++;
                }
                if (j - a < 2) continue;
                double *run = malloc(sizeof(double) * 3 * (j - a));
                for (int q = a; q < j; q++) { run[3 * (q - a)] = L->xy[2 * PT(q)]; run[3 * (q - a) + 1] = L->xy[2 * PT(q) + 1]; run[3 * (q - a) + 2] = z; }
                if (wl_pd) { fprintf(wl_pd, "PASS %d%s\n", j - a, nin == n ? " closed" : ""); for (int q = 0; q < j - a; q++) fprintf(wl_pd, "%.17g %.17g %.17g\n", run[3 * q] * u, run[3 * q + 1] * u, run[3 * q + 2] * u); }   /* testing (MCAM_WL_PASSDUMP) */
                if (wlo) {                                                       /* collected for the linker's order (mm) */
                    if (nw == capw) { capw = capw ? 2 * capw : 64; W = realloc(W, sizeof(WlPass) * capw); }
                    WlPass *P = &W[nw++]; memset(P, 0, sizeof *P); P->n = j - a; P->closed = nin == n; P->node = lnode && !wl_fail ? lnode[i] : -1;
                    P->p = run; for (int q = 0; q < 3 * P->n; q++) P->p[q] *= u;
                    if (lnode && !wl_fail && lnode[i] >= 0) {                     /* the region's loops at this level (mm): FUN_1801a1240's set */
                        int tot = 0; for (int t = 0; t < PL->n; t++) if (lnode[t] == lnode[i]) { P->nrg++; tot += PL->p[t].n; }
                        P->rg = malloc(sizeof(double) * 2 * (tot ? tot : 1)); P->rgn = malloc(sizeof(int) * (P->nrg ? P->nrg : 1)); int o = 0, kk = 0;
                        for (int t = 0; t < PL->n; t++) if (lnode[t] == lnode[i]) { P->rgn[kk++] = PL->p[t].n; for (int q = 0; q < 2 * PL->p[t].n; q++) P->rg[o++] = PL->p[t].xy[q] * u; }
                    }
                    if (nin < n && nreg == n && (!vsp || vo.closed[i])) {      /* an open pass (the vertex filter's) of a closed loop: its toolpath item */
                        P->nitem = n + 1; P->item = malloc(sizeof(double) * 3 * (n + 1));
                        for (int q = 0; q <= n; q++) { int v = PT(q % n); P->item[3 * q] = L->xy[2 * v] * u; P->item[3 * q + 1] = L->xy[2 * v + 1] * u; P->item[3 * q + 2] = z * u; }
                    }
                    continue;
                }
                C3Emit em = { b, fp, plunge, feed, &down, NULL, NULL, 0 };
                int npc = c3_chk_emit(chk, run, j - a, nin == n, &em);   /* a whole loop is a closed pass */
                free(run);
                if (npc) { V3 e = lastp(b); cx = e.x; cy = e.y; nloops += npc; }
            }
            #undef PT
            #undef SPK
            free(in);
        }
        if (lnode) {                                                             /* this level becomes the previous one */
            cl_free(&wl_prev); free(wl_prev_node); free(wl_prev_dep);
            wl_prev.n = PL->n; wl_prev.p = calloc(PL->n, sizeof(CPath));
            for (int i = 0; i < PL->n; i++) { wl_prev.p[i].n = PL->p[i].n; wl_prev.p[i].xy = malloc(sizeof(double) * 2 * (PL->p[i].n ? PL->p[i].n : 1)); memcpy(wl_prev.p[i].xy, PL->p[i].xy, sizeof(double) * 2 * PL->p[i].n); }
            wl_prev_node = hnode; wl_prev_dep = ldep; hnode = NULL; ldep = NULL;
        }
        free(lnode); free(hnode); free(ldep);
        cl_free(&forb); WL_RAWFREE(); wlvor_out_free(&vo);
        #undef WL_RAWFREE
    }
    cl_free(&wl_prev); free(wl_prev_node); free(wl_prev_dep);
    /* MCAM_HMM_PASSES=PREFIX.mmpasses + MCAM_HMM_WLGRAPH=PREFIX.wlgraph (testing, formats/hst_linker.md 22.2): the DLL's own passes (tag 1, mm) and its
       region graph (tools/tests/wl_graph.py on an HMM_TRACE of FUN_1802ed200 / FUN_1802ed2c0 / FUN_1801c05c0) replace the port's, so the port's linker
       runs on exactly the DLL's input */
    if (wlo && okall && getenv("MCAM_HMM_PASSES") && getenv("MCAM_HMM_WLGRAPH")) {
        for (int i = 0; i < nw; i++) { free(W[i].p); free(W[i].succ); free(W[i].pre); free(W[i].item); free(W[i].rg); free(W[i].rgn); }
        for (int i = 0; i < nn; i++) free(N[i].par);
        nw = nn = 0; wl_fail = 0;
        FILE *gf = fopen(getenv("MCAM_HMM_WLGRAPH"), "r"), *pf = fopen(getenv("MCAM_HMM_PASSES"), "r");
        char ln[256]; int a, c2, fi = 0;
        if (!gf || !pf) { snprintf(op->error, sizeof op->error, "MCAM_HMM_PASSES / MCAM_HMM_WLGRAPH: cannot read"); okall = 0; }
        while (okall && fgets(ln, sizeof ln, gf)) {
            if (sscanf(ln, "N %d", &a) == 1) { N = realloc(N, sizeof(WlNode) * (a ? a : 1)); capn = nn = a; for (int i = 0; i < a; i++) { N[i].level = 0; N[i].h = i; N[i].npar = 0; N[i].par = NULL; N[i].z = NAN; } }
            else if (sscanf(ln, "E %d %d", &a, &c2) == 2 && a >= 0 && a < nn) { N[a].par = realloc(N[a].par, sizeof(int) * (N[a].npar + 1)); N[a].par[N[a].npar++] = c2; }
        }
        double sc = 1.0; { const char *hp = getenv("MCAM_HMM_PASSES"); size_t hl = strlen(hp); if (!(hl > 9 && !strcmp(hp + hl - 9, ".mmpasses"))) sc = u; }
        int *fnode = NULL, nf = 0; if (gf) { rewind(gf); while (fgets(ln, sizeof ln, gf)) if (sscanf(ln, "F %d", &a) == 1) { fnode = realloc(fnode, sizeof(int) * (nf + 1)); fnode[nf++] = a; } }
        while (okall && fgets(ln, sizeof ln, pf)) {
            int n; if (sscanf(ln, "PASS %d", &n) != 1 || n < 2) continue;
            if (nw == capw) { capw = capw ? 2 * capw : 64; W = realloc(W, sizeof(WlPass) * capw); }
            WlPass *P = &W[nw]; memset(P, 0, sizeof *P); P->n = n; P->p = malloc(sizeof(double) * 3 * n);
            for (int k = 0; k < n && fgets(ln, sizeof ln, pf); k++) { sscanf(ln, "%lf %lf %lf", &P->p[3 * k], &P->p[3 * k + 1], &P->p[3 * k + 2]); for (int t = 0; t < 3; t++) P->p[3 * k + t] *= sc; }
            P->closed = wl_d3(P->p, P->p + 3 * n - 3) < 1e-18;   /* the closing gap (a closed frag ends where it starts) */
            P->node = fi < nf ? fnode[fi] : -1; fi++; nw++;
        }
        free(fnode); if (gf) fclose(gf); if (pf) fclose(pf);
        wl_arctan = 1;
        for (int i = 0; i < nw; i++) if (W[i].node >= 0 && W[i].node < nn) N[W[i].node].z = W[i].p[2];   /* a region's level: its passes' Z (a region
                                                                                   without passes keeps NAN: its toolpath Z is not in the DLL's dump) */
        /* MCAM_WL_LOOPS=FILE (testing): the DLL's region loops (scratch gloops.py from an HMM_TRACE 180017470:g: FUN_180017470's GridC 0x1e per toolpath,
           "LOOP z n" + n "x y", mm): a pass takes the loop at its Z through its start */
        FILE *lf = okall && getenv("MCAM_WL_LOOPS") ? fopen(getenv("MCAM_WL_LOOPS"), "r") : NULL;
        while (lf && fgets(ln, sizeof ln, lf)) {
            double lz; int n; if (sscanf(ln, "LOOP %lf %d", &lz, &n) != 2 || n < 3) continue;
            double *q = malloc(sizeof(double) * 2 * n);
            for (int k = 0; k < n && fgets(ln, sizeof ln, lf); k++) sscanf(ln, "%lf %lf", &q[2 * k], &q[2 * k + 1]);
            for (int i = 0; i < nw; i++) {
                WlPass *P = &W[i]; if (fabs(P->p[2] - lz) > 1e-6) continue;
                double bd = 1e300; for (int k = 0; k + 1 < n; k++) { double A[3] = { q[2 * k], q[2 * k + 1], 0 }, B[3] = { q[2 * k + 2], q[2 * k + 3], 0 }, Q[3] = { P->p[0], P->p[1], 0 };
                    double d2 = c3_seg_d2(Q, A, B); if (d2 < bd) bd = d2; }
                if (bd > 1e-4) continue;
                free(P->rg); free(P->rgn); P->nrg = 1; P->rgn = malloc(sizeof(int)); P->rgn[0] = n; P->rg = malloc(sizeof(double) * 2 * n); memcpy(P->rg, q, sizeof(double) * 2 * n);
            }
            free(q);
        }
        if (lf) fclose(lf);
        /* MCAM_HMM_ITEMS=FILE (testing): the toolpath items of the DLL's open passes - the passes of a DLL run without the contact-angle split
           (tools/hmm_macro.py HMM_CONTACT_LO=0, the same "PASS n" format, mm): an open pass takes the closed one at its Z through its end */
        FILE *itf = okall && getenv("MCAM_HMM_ITEMS") ? fopen(getenv("MCAM_HMM_ITEMS"), "r") : NULL;
        while (itf && fgets(ln, sizeof ln, itf)) {
            int n; if (sscanf(ln, "PASS %d", &n) != 1 || n < 2) continue;
            double *q = malloc(sizeof(double) * 3 * n);
            for (int k = 0; k < n && fgets(ln, sizeof ln, itf); k++) sscanf(ln, "%lf %lf %lf", &q[3 * k], &q[3 * k + 1], &q[3 * k + 2]);
            int used = 0;
            if (wl_d3(q, q + 3 * n - 3) < 1e-18) for (int i = 0; i < nw; i++) {
                WlPass *P = &W[i]; const double *e = P->p + 3 * P->n - 3;
                if (P->closed || P->item || fabs(e[2] - q[2]) > 1e-6) continue;
                double f[3], tt; int k0; wl_npt3(q, n, e, f, &k0, &tt);          /* on the item (to its arcs' chords: 1e-3 mm) */
                if (wl_d3(f, e) > 1e-6) continue;
                const double *e1 = P->p + 3 * P->n - 6;                          /* the item's direction = the pass's (its last segment) */
                double tx = e[0] - e1[0], ty = e[1] - e1[1], ix = q[3 * k0 + 3] - q[3 * k0], iy = q[3 * k0 + 4] - q[3 * k0 + 1];
                P->nitem = n; P->item = malloc(sizeof(double) * 3 * n);
                for (int k = 0; k < n; k++) memcpy(P->item + 3 * k, q + 3 * (tx * ix + ty * iy >= 0 ? k : n - 1 - k), sizeof(double) * 3);
                used = 1;
            }
            free(q); (void)used;
        }
        if (itf) fclose(itf);
    }
    if (wl_pd) fclose(wl_pd);
    if (wlo && okall) wl_link_hst(W, nw, N, nn, wl_fail, b, fp, plunge, feed, &down, u, &M, R, Rc, sxy, sz, tol, D, wl_maxstay, wl_safez, wl_tr, wl_ramp, &nloops, cx, cy, &wlead);
    for (int i = 0; i < nw; i++) { free(W[i].p); free(W[i].succ); free(W[i].pre); free(W[i].item); free(W[i].rg); free(W[i].rgn); }
    free(W); for (int i = 0; i < nn; i++) free(N[i].par); free(N);
    cl_free(&region); c3_mesh_free(&M); c3_chk_close(chk); if (lim) wltg_free(&tg); free(zl);
    if (!okall) return 0;
    if (!nloops || !b->n) { snprintf(op->error, sizeof op->error, "nothing to cut (%d levels)", nz); return 0; }
    { V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance > fp ? p->clearance : fp, 0); }
    return 1;
}

/* ------------------------------------------------------------------------------------------------------- Area Roughing */
/* HST Area Roughing (UI style 0, kernel style 0). Decode: formats/hst3d_port_plan.md 9 (cited "ar N"; mk = decomp/mckernel_all.c, hp =
   decomp/hmm_pass0.c, hz = decomp/hmm_zplan.c). The passes plan is Core Roughing 0x1460 when Core (record 0x16ef = TP+0x486) is on, else
   Area Clearance 0x1435 (FUN_18006fb24 / FUN_18006fb04 on TP+0x1a8, mk l.93390-93409). Both evaluators run FUN_1803fabf0 with pass kind 2
   (Eva +0xff0 = 1 in both constructors: EvaCoreRoughingPassesPlan FUN_1800c7b80, EvaAreaClearancePlan FUN_18040fa80), so the passes of
   a level are McHmm's Area Clearance offsets of the tool-centre region (formats/area_mill.md 11.3, area_mill_passes). Inputs (FUN_180071314
   style 0, mk l.87696-87820): Stepdown 0x1228; MinOffset / MaxOffset = min / max of TP+0x230 / 0x238 = record 0x12b8 / 0x12c0 (page 1326
   "Minimum" / "Maximum", formats/ui_fields_mcmsurf.tsv); StepDownEvenLastSteps the plan default 5 (FUN_18030b160 hp l.596-601, no override
   for style 0); ToolThicknessXY = max(XY, Z stock) (the non-Horizontal branch, mk l.87571-87596).
   Core (ar 4): FUN_180071120 (mk l.86896) gives the constraint boundaries an outward OffsetDistance (ekPlanTcb3x / the Bounding Box Plan,
   mk l.92851-92990); the passes are kept inside them (measured on both stored Core ops; CoreRoughingTrimOffset, hp l.1497-1508, belongs to
   the trim-boundaries input mckernel's 3D macro does not give).
   Ours (labelled): the drive tessellation (OCCT); Core's passes region (a box around the constraint boundary: Core's own region is NOT
   DECODED - FUN_1802e9fb0's CoreRoughingExpansion box, hp l.2995-3020, belongs to a plan reading the passes, i.e. the links); keeping Core's
   passes inside the grown constraint boundary (what FUN_1802ef450 does without CoreRoughingTrimBoundaries is NOT DECODED - measured,
   formats/hst3d_port_plan.md 9.5); the slice only over the trim
   region; the pass order (inner first, as the 2D port); the entries (the 2D profile ramp of cam2d_area.c on closed passes; an open Core
   pass starting outside the containment plunges there, else ramps along itself); the SAFE links. NOT PORTED (refused): Add cuts, Corner
   rounding. NOT DETERMINED (not generated): the Horizontal Area plan mckernel adds when TP+0x475 = 2 (its record field is not mapped). */
/* FUN_180071120 (mk l.86896-86960): the Core boundary offset; eps = MCSystemContext +0x10 (the system tolerance, area_mill.h) */
static double ar_core_offset(double D, double Rc, double mx, double sxy, double sz, double eps) {
    double d4 = D * 0.5 - eps > mx ? D * 0.5 - mx : 0.0;
    double d3 = sxy <= -Rc ? D : D + sxy * 2.0;
    double d5 = Rc + sz >= 0.0 ? Rc + sz : 0.0;
    double a = fmax(0.0, mx - (d3 * 0.5 - d5)), c = fmax(0.0, mx - (D * 0.5 - Rc)) - a;
    if (!(c > eps)) c = 0.0;
    return d4 > eps || c > eps ? fmax(d4, c) : 0.0;
}
static void ar_bbox(const CPaths *r, double *bx) {
    bx[0] = bx[1] = 1e300; bx[2] = bx[3] = -1e300;
    for (int i = 0; i < r->n; i++) for (int k = 0; k < r->p[i].n; k++) { bx[0] = fmin(bx[0], r->p[i].xy[2 * k]); bx[1] = fmin(bx[1], r->p[i].xy[2 * k + 1]); bx[2] = fmax(bx[2], r->p[i].xy[2 * k]); bx[3] = fmax(bx[3], r->p[i].xy[2 * k + 1]); }
}
/* ours: an open pass entered inside the material - descend from z0 to z going forward along it for the profile ramp's length (am_ramp's n x
   ProfileRampScanForward), back and forth when it is shorter, then back to its start at z */
static void ar_ramp_open(MB *b, const double *xy, int n, double z0, double z, double R, double ramp_deg, double feed) {
    double scan = 1.1 * R, drop = scan * tan(ramp_deg * M_PI / 180);
    int ns = drop > 1e-9 ? (int)((z0 - z) / drop + 1.0) : 1; if (ns < 1) ns = 1;
    double L = ns * scan, done = 0, len = 0;
    for (int i = 1; i < n; i++) len += hypot(xy[2 * i] - xy[2 * i - 2], xy[2 * i + 1] - xy[2 * i - 1]);
    mv(b, 0, xy[0], xy[1], z0, feed);
    if (len < 1e-9) { mv(b, 0, xy[0], xy[1], z, feed); return; }
    int i = 0, dir = 1;
    for (int guard = 0; done < L - 1e-9 && guard < 100000; guard++) {
        int j = i + dir; if (j < 0 || j >= n) { dir = -dir; continue; }
        double l = hypot(xy[2 * j] - xy[2 * i], xy[2 * j + 1] - xy[2 * i + 1]);
        if (done + l >= L) { double t = l > 0 ? (L - done) / l : 1; mv(b, 0, xy[2 * i] + (xy[2 * j] - xy[2 * i]) * t, xy[2 * i + 1] + (xy[2 * j + 1] - xy[2 * i + 1]) * t, z, feed); done = L; break; }
        done += l; mv(b, 0, xy[2 * j], xy[2 * j + 1], z0 - (z0 - z) * done / L, feed); i = j;
    }
    /* back to the start at depth along the pass */
    { V3 e = lastp(b); int k = i; (void)e; while (k > 0) { k--; mv(b, 0, xy[2 * k], xy[2 * k + 1], z, feed); } }
}
static int gen_arearough(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "Area Roughing here needs the Top tool plane at the origin"); return 0; }
    uint32_t rn = 0; uint8_t *rec = hst_record(f, p, NULL, &rn);
    if (!rec || rn < 0x1760) { free(rec); snprintf(op->error, sizeof op->error, "no Area Roughing record (Mastercam's default op 132 style 0 is missing)"); return 0; }
    #define RD(o) c3d(rec, rn, o)
    #define RB(o) c3b(rec, rn, o)
    const char *why = NULL;
    /* Corner rounding: McHmm's pass positions use CornerOffsetTolerance (decoded, below); the loop smoothing FUN_180557f60 with
       ProfileSmoothingMaxRadius is NOT PORTED - refused, except for the diagnostic MCAM_AR_SMOOTH_DIAG (ours, testing: positions only) */
    int smooth = RB(0x16c3), sdiag = getenv("MCAM_AR_SMOOTH_DIAG") != NULL;
    if (RB(0x16c2)) why = "Add cuts (AdaptiveStepdown): NOT PORTED";
    else if (smooth && !sdiag) why = "Corner rounding (ProfileSmoothing): NOT PORTED";
    if (why) { snprintf(op->error, sizeof op->error, "Area Roughing: %s", why); free(rec); return 0; }
    int metric = mcam_is_metric(f);
    double D = p->tool_dia > 0 ? p->tool_dia : RD(0x350), R = D * 0.5;
    double Rc = fmin(fmax(p->tool_dia > 0 ? p->corner_r : RD(0x358), 0.0), R);
    double sd = RD(0x1228); if (!(sd > 1e-12)) sd = metric ? 2.5 : 0.1;          /* Stepdown TP+0x1b0 (mk l.87353-87364) */
    double mn = fmin(RD(0x12b8), RD(0x12c0)), mx = fmax(RD(0x12b8), RD(0x12c0)); /* MinOffset / MaxOffset (mk l.87724-87753) */
    double sxy0 = RD(0x13c0), sz = RD(0x13c8), sxy = fmax(sxy0, sz);
    double ztol = RD(0x12a0), tol = c3_step_tol(rec, rn, metric);                   /* StepMachTolerance (FUN_18006dd68, c3_step_tol) */
    /* ProfileSmoothingCornerOffsetTolerance = max(TP+0x240, 2 x job +0xa0) (mk l.87386-87389; TP+0x240 = record 0x12c8, page 1326 control 275
       "Offset tolerance"); job +0xa0 = the cut tolerance */
    double tau = smooth ? fmax(RD(0x12c8), 2 * tol) : 0.0;
    int core = RB(0x16ef);
    int conventional = RB(0x16e4);   /* page 1326 Cut method 1239: Climb / Conventional, 0x16e4 = (index == 1) (MapToDlg FUN_180087460 /
                                        UpdateLocalOp FUN_180087880, decomp/mcmsurf_all.c l.88380-88390 / 88526-88549) */
    double ramp_deg = RD(0x1338) * 180.0 / M_PI; if (!(ramp_deg > 0.01)) ramp_deg = 3.0;   /* page 1332 Plunge angle (radians) */
    int zdep = RB(0x16c9); double zd1 = RD(0x1238), zd2 = RD(0x1240);
    free(rec);
    #undef RD
    #undef RB
    if (!(D > 0)) { snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); return 0; }
    if (!(mn > 1e-9)) { snprintf(op->error, sizeof op->error, "Area Roughing: the XY stepover Minimum / Maximum must be > 0"); return 0; }
    const double eps = CAM_SYS_TOL;                                                 /* MCSystemContext +0x10 = system tol (area_mill.md: MCSystemContext::MCSystemContext) */
    C3Mesh M; char err[150];
    if (!c3_mesh_op(f, op, ztol > 1e-9 ? ztol : tol * 0.1, &M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); return 0; }
    if (fmin(sxy, sz) + Rc < -ztol) { snprintf(op->error, sizeof op->error, "negative stock to leave below the corner radius: NOT DECODED"); c3_mesh_free(&M); return 0; }
    /* PosZHi / PosZLo (hst3d_port_plan.md 6.7; style 0 is not in the mask 0x5b6); style 0 adds half the tolerance to PosZHi when the range is
       below the system tolerance (mk l.87702-87716) */
    double zhi = M.hi[2], zlo = M.lo[2];
    if (sz > 0) zhi += sz; else if (sz < 0) zlo += sz;
    zhi += ztol; zlo -= ztol;
    if (zdep) { zhi = fmax(zd1, zd2); zlo = fmin(zd1, zd2); }
    if (fabs(zhi - zlo) < eps) zhi += tol * 0.5;
    int flat_tip = Rc + sz <= 0.0;
    double Rb = flat_tip ? 0.0 : Rc + sz, Fo = flat_tip ? R + sxy : (R - Rc) + sxy - sz;
    double spacing = c3_resolution(R, Rc, sz, tol); if (!(spacing > 0)) spacing = R;
    double arc_tol = fmax(tol * 0.5, 1e-4), unit = metric ? 1.0 : 1.0 / 25.4;
    /* the constraint boundary: the containment with the tool containment rule (as Waterline), else the drive's XY box (the Bounding Box Plan
       of the surfaces, mk l.92875) */
    CPaths C = { 0 };
    { char ce[150]; if (!c3_containment(f, op, c3_hst_tcb_type(op), &C, ce, sizeof ce)) { snprintf(op->error, sizeof op->error, "%s", ce); c3_mesh_free(&M); return 0; } }   /* the op's chains as tmptc.tmp holds them */
    { uint32_t rn2 = 0; uint8_t *r2 = hst_record(f, p, NULL, &rn2); if (r2) { int ok = c3_tcb_offset(&C, r2, rn2, D, arc_tol); free(r2); if (!ok) { snprintf(op->error, sizeof op->error, C3_TCB_FAIL); cl_free(&C); c3_mesh_free(&M); return 0; } } }   /* the containment as written to tmptc.tmp (c3_tcb_offset) */
    if (!C.n) { C.p = calloc(1, sizeof(CPath)); C.p[0] = c3_box_path(M.lo[0], M.lo[1], M.hi[0], M.hi[1]); C.n = 1; }
    /* Core: the constraint boundary grown by FUN_180071120's offset (ekPlanTcb3x, OffsetDirection 1); the passes are kept inside it. The
       CoreRoughingTrimOffset (max(0, MaxOffset - ToolFlatRadius)) applies to CoreRoughingTrimBoundaries, which mckernel's 3D macro does not
       give (mk l.93229-93243): not applied. Measured (hst3d_port_plan.md 9.5): TUTORIAL 2 SWARF_METRIC op 1 keeps a ring 0.9 inside the
       containment and drops one 3.9 beyond; MOLD BOTTLE op 1 drops one 0.07 in beyond (its trim offset is 0.0875, its Core offset 0.0375) */
    double coff = core ? ar_core_offset(D, Rc, mx, sxy0, sz, eps) : 0.0, trim = 0.0;
    if (coff > 0) { CPaths o = cl_offset_nz(&C, coff, arc_tol); cl_free(&C); C = o; }
    if (getenv("MCAM_AR_TRIM")) trim = atof(getenv("MCAM_AR_TRIM"));                  /* ours (testing): an extra trim distance */
    CPaths Ct = trim > 0 ? cl_offset_nz(&C, trim, arc_tol) : cl_bool(&C, NULL, 0);
    double cb[4], bx[4]; ar_bbox(&C, cb); ar_bbox(&Ct, bx);
    CPaths big = { 0 };
    /* Core's pass region: the constraint box widened by ex x its size on each side - ours: FUN_1802e9fb0's CoreRoughingExpansion rule (1.0) is
       7x slower here and coarser on area_mill_passes' 2e6-cell grid; 0.2 (MCAM_AR_EXP, testing) */
    double ex = getenv("MCAM_AR_EXP") ? atof(getenv("MCAM_AR_EXP")) : 0.2;
    if (core) { double w = (cb[2] - cb[0]) * ex, h = (cb[3] - cb[1]) * ex; big.p = calloc(1, sizeof(CPath)); big.p[0] = c3_box_path(cb[0] - w, cb[1] - h, cb[2] + w, cb[3] + h); big.n = 1;
        double g = R + fabs(sxy) + 2 * spacing; bx[0] -= g; bx[1] -= g; bx[2] += g; bx[3] += g; }
    /* the levels: Area's StepDownEvenLastSteps 5 (hp l.596-601) and CZInflexions (c3_levels_infl; Avoidance default) */
    double *zl = NULL; int nz = c3_levels_infl(&M, cb, sz, zhi, zlo, sd, 5, -1, unit, &zl);
    if (nz < 0) { snprintf(op->error, sizeof op->error, nz == -1 ? "cancelled" : "the Z levels do not descend (the stepdown is below the numeric precision of the Z range)"); free(zl); cl_free(&C); cl_free(&Ct); cl_free(&big); c3_mesh_free(&M); return 0; }
    if (getenv("MCAM_3D_LEVELS")) { for (int k = 0; k < nz; k++) fprintf(stderr, "level %.4f\n", zl[k]); }   /* ours (testing) */
    if (getenv("MCAM_3D_DEBUG")) cam3d_debug = 1;
    if (cam3d_debug) fprintf(stderr, "arearough: %s, %d levels %.4f..%.4f, min %.4f max %.4f, core offset %.4f trim %.4f, constraint box %.4f %.4f .. %.4f %.4f\n",
                             core ? "Core" : "Area Clearance", nz, zhi, zlo, mn, mx, coff, trim, cb[0], cb[1], cb[2], cb[3]);
    double fp = p->top_z + p->feed_plane; if (fp < zhi + p->feed_plane) fp = zhi + p->feed_plane;
    double feed = p->feed > 0 ? p->feed : 100, plunge = p->plunge_feed > 0 ? p->plunge_feed : feed;
    int okall = 1, npass = 0, down = 0; double zprev = fmax(p->top_z, zhi);
    for (int k = 0; k < nz && okall; k++) {
        double z = zl[k];
        /* the first level is PosZHi itself (FUN_18040da70 +0x2b9): cut only when the top of stock is above it (as OptiRough, cam3d_rough.c) */
        if (k == 0 && !(p->top_z > z + 1e-9)) continue;
        if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); okall = 0; break; }
        clock_t c0 = clock();
        CPaths forb = c3_slice(&M, z - sz, Rb, z + Rc, Fo, spacing, bx, arc_tol);   /* the weave Z = level - c, c = Eva +0x140 = ToolThicknessZ (FUN_1802f4c80): FUN_18040cf60 hz l.13009, FUN_180413d90 hz l.16662, set at CWeave +0x158 by FUN_1801298a0; a triangle counts when h = zmax - z_weave > 0 (FUN_1801295b0) */
        if (cam3d_debug) fprintf(stderr, "  level %.4f: slice %.2f s\n", z, (double)(clock() - c0) / CLOCKS_PER_SEC);
        CPaths T = forb.n ? cl_bool(core ? &big : &C, &forb, 2) : cl_bool(core ? &big : &C, NULL, 0);
        cl_free(&forb);
        if (!T.n || cl_total_area(&T) < 1e-9) { cl_free(&T); continue; }
        if (cam3d_debug) { int nsmall = 0; for (int i = 0; i < T.n; i++) { CPaths one = { &T.p[i], 1 }; if (fabs(cl_total_area(&one)) < R * R) nsmall++; }
            fprintf(stderr, "  level %.4f: T %d loops (%d below R^2), area %.3f\n", z, T.n, nsmall, cl_total_area(&T)); }
        AreaMillPasses ps; clock_t c1 = clock();
        if (!area_mill_passes(&T, mn, mn, mx - mn, tau, arc_tol, unit, &ps)) { cl_free(&T); continue; }
        if (cam3d_debug) fprintf(stderr, "  level %.4f: %d passes (%.2f s)\n", z, ps.n, (double)(clock() - c1) / CLOCKS_PER_SEC);
        CPaths Tl = cl_offset_nz(&T, 0.002 * (metric ? 1 : 1 / 25.4), arc_tol);
        cl_free(&T);
        double z0 = zprev + 0.1 * unit;                                              /* the 2D port's measured ramp start above the previous level */
        for (int i = 0; i < ps.n; i++) for (int q = 0; q < ps.p[i].loops.n; q++) {
            CPath *L = &ps.p[i].loops.p[q]; if (L->n < 3) continue;
            if (conventional) reverse_path(L);
            if (!core) {   /* Area Clearance: closed passes */
                if (down) { V3 e = lastp(b); rotate_to(L, e.x, e.y); }
                if (down && seg_inside(&Tl, lastp(b).x, lastp(b).y, L->xy[0], L->xy[1])) mv(b, 0, L->xy[0], L->xy[1], z, feed);
                else { if (down) { V3 e = lastp(b); mv(b, 1, e.x, e.y, fp, 0); }
                    CPath rl = { malloc(sizeof(double) * 2 * L->n), L->n }; memcpy(rl.xy, L->xy, sizeof(double) * 2 * L->n);
                    mv(b, 1, L->xy[0], L->xy[1], fp, 0); mv(b, 1, L->xy[0], L->xy[1], z0, 0);
                    am_ramp(b, &rl, z0, z, R, ramp_deg, plunge); free(rl.xy); }
                cut_loop(b, L, z, feed); down = 1; npass++;
                continue;
            }
            /* Core: the parts of the pass inside the trim region (open runs; a fully inside loop stays closed) */
            int n = L->n; char *in = malloc(n); int nin = 0;
            for (int j = 0; j < n; j++) { in[j] = (char)wl_inside(&Ct, L->xy[2 * j], L->xy[2 * j + 1]); nin += in[j]; }
            if (!nin) { free(in); continue; }
            if (nin == n) {
                if (down) { V3 e = lastp(b); rotate_to(L, e.x, e.y); }
                if (down && seg_inside(&Tl, lastp(b).x, lastp(b).y, L->xy[0], L->xy[1])) mv(b, 0, L->xy[0], L->xy[1], z, feed);
                else { if (down) { V3 e = lastp(b); mv(b, 1, e.x, e.y, fp, 0); }
                    mv(b, 1, L->xy[0], L->xy[1], fp, 0);
                    if (!wl_inside(&C, L->xy[0], L->xy[1])) mv(b, 0, L->xy[0], L->xy[1], z, plunge);
                    else { CPath rl = { malloc(sizeof(double) * 2 * n), n }; memcpy(rl.xy, L->xy, sizeof(double) * 2 * n);
                        mv(b, 1, L->xy[0], L->xy[1], z0, 0); am_ramp(b, &rl, z0, z, R, ramp_deg, plunge); free(rl.xy); } }
                cut_loop(b, L, z, feed); down = 1; npass++; free(in); continue;
            }
            int s0 = 0; for (int j = 0; j < n; j++) if (!in[j]) { s0 = j; break; }
            double *run = malloc(sizeof(double) * 2 * (n + 1));
            int j = 0;
            while (j < n) {
                while (j < n && !in[(s0 + j) % n]) j++;
                if (j >= n) break;
                int a = j, m = 0; while (j < n && in[(s0 + j) % n]) { int v = (s0 + j) % n; run[2 * m] = L->xy[2 * v]; run[2 * m + 1] = L->xy[2 * v + 1]; m++; j++; }
                if (m < 2) continue;
                (void)a;
                if (down && seg_inside(&Tl, lastp(b).x, lastp(b).y, run[0], run[1])) mv(b, 0, run[0], run[1], z, feed);
                else { if (down) { V3 e = lastp(b); mv(b, 1, e.x, e.y, fp, 0); }
                    mv(b, 1, run[0], run[1], fp, 0);
                    if (!wl_inside(&C, run[0], run[1])) mv(b, 0, run[0], run[1], z, plunge);
                    else { mv(b, 1, run[0], run[1], z0, 0); ar_ramp_open(b, run, m, z0, z, R, ramp_deg, plunge); } }
                for (int t = 1; t < m; t++) mv(b, 0, run[2 * t], run[2 * t + 1], z, feed);
                down = 1; npass++;
            }
            free(run); free(in);
        }
        area_mill_passes_free(&ps); cl_free(&Tl);
        zprev = z;
    }
    cl_free(&C); cl_free(&Ct); cl_free(&big); c3_mesh_free(&M); free(zl);
    if (!okall) return 0;
    if (!npass || !b->n) { snprintf(op->error, sizeof op->error, "nothing to cut (%d levels)", nz); return 0; }
    { V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance > fp ? p->clearance : fp, 0); }
    return 1;
}


/* ------------------------------------------------------------------------------------------------------- the drop cutter */
/* CToolDropPos (mchmm.dll, walked by FUN_180223650 over the triangles): the highest tool tip whose tool (flat radius Fo, corner radius Rb, the stock-inflated tool of FUN_180222920's profile) touches
   a triangle, as the maximum of the vertex (FUN_180112d30), facet (FUN_180113220) and edge (FUN_180113ce0) tests, hst3d_weave_drop.c. The tool profile: h(r) = 0 for r <= Fo, Rb - sqrt(Rb^2 - (r - Fo)^2) to r = Fo + Rb.
   The edge test for a torus tool: FUN_180113ce0 solves a quartic (FUN_1802e6e10, its arguments lost by the decompiler); here the same contact as the maximum of the concave z(s) - h(r(s)) along the edge (ours: golden-section, not the quartic). */
typedef struct { WlTg tg; double Fo, Rb, reach; } HstDrop;
static void hd_build(HstDrop *d, const C3Mesh *M, double Fo, double Rb) { d->Fo = Fo; d->Rb = Rb; d->reach = Fo + Rb; wltg_build(&d->tg, M, fmax(d->reach * 0.5, 1e-6)); }
static void hd_free(HstDrop *d) { wltg_free(&d->tg); }
static double hd_prof(const HstDrop *d, double r) { double u = r - d->Fo; if (u <= 0) return 0.0; return d->Rb - sqrt(fmax(d->Rb * d->Rb - u * u, 0.0)); }
static double hd_tip(const HstDrop *d, double x, double y) {
    const WlTg *g = &d->tg; const C3Mesh *M = g->M; double reach = d->reach, r2max = reach * reach, best = -1e30;
    int i0 = (int)floor((x - reach - g->x0) / g->cell), i1 = (int)floor((x + reach - g->x0) / g->cell), j0 = (int)floor((y - reach - g->y0) / g->cell), j1 = (int)floor((y + reach - g->y0) / g->cell);
    #define VERT(P) do { double dx_ = (P)[0] - x, dy_ = (P)[1] - y, r2_ = dx_ * dx_ + dy_ * dy_; if (r2_ <= r2max) { double z_ = (P)[2] - hd_prof(d, sqrt(r2_)); if (z_ > best) best = z_; } } while (0)
    for (int j = j0; j <= j1; j++) { if (j < 0 || j >= g->ny) continue;
    for (int i = i0; i <= i1; i++) { if (i < 0 || i >= g->nx) continue;
    for (int e = g->head[j * g->nx + i]; e >= 0; e = g->next[e]) {
        const double *v = M->t + 9 * g->tri[e], *A = v, *B = v + 3, *C = v + 6;
        double xl = fmin(A[0], fmin(B[0], C[0])), xh = fmax(A[0], fmax(B[0], C[0])), yl = fmin(A[1], fmin(B[1], C[1])), yh = fmax(A[1], fmax(B[1], C[1]));
        if (xl > x + reach || xh < x - reach || yl > y + reach || yh < y - reach) continue;
        if (fmax(A[2], fmax(B[2], C[2])) <= best) continue;
        VERT(A); VERT(B); VERT(C);
        /* the facet (FUN_180113220): the contact offset from the axis along the steepest direction */
        double e0 = B[0] - A[0], e1 = B[1] - A[1], e2 = B[2] - A[2], f0 = C[0] - A[0], f1 = C[1] - A[1], f2 = C[2] - A[2];
        double nz = e0 * f1 - e1 * f0;
        if (nz != 0.0) {
            double nx = e1 * f2 - e2 * f1, ny = e2 * f0 - e0 * f2;
            if (nz < 0) { nz = -nz; nx = -nx; ny = -ny; }
            double hn = hypot(nx, ny), L = sqrt(hn * hn + nz * nz), cx = 0, cy = 0, dv = 0;
            if (d->Rb > 0) { cx = -nx * d->Rb / L; cy = -ny * d->Rb / L; dv = (1 - nz / L) * d->Rb; }
            if (hn > 0) { cx += -nx * d->Fo / hn; cy += -ny * d->Fo / hn; }
            double px = x + cx - A[0], py = y + cy - A[1], den = e0 * f1 - e1 * f0;
            double u = (px * f1 - py * f0) / den, w = (e0 * py - e1 * px) / den;
            if (u >= 0 && w >= 0 && u + w <= 1) { double z = A[2] + u * e2 + w * f2 - dv; if (z > best) best = z; }
        }
        /* the edges (FUN_180113ce0): the maximum of z(s) - h(r(s)) over the part of the edge within reach */
        const double *E[3][2] = { { A, B }, { B, C }, { C, A } };
        for (int q = 0; q < 3; q++) { const double *P = E[q][0], *Q = E[q][1];
            if (fmax(P[2], Q[2]) <= best) continue;
            double ex = Q[0] - P[0], ey = Q[1] - P[1], ez = Q[2] - P[2], ll = ex * ex + ey * ey; if (ll == 0) continue;
            double t0 = ((x - P[0]) * ex + (y - P[1]) * ey) / ll, px = P[0] + t0 * ex - x, py = P[1] + t0 * ey - y, d2 = px * px + py * py;
            if (d2 > r2max) continue;
            double half = sqrt((r2max - d2) / ll), s0 = fmax(t0 - half, 0.0), s1 = fmin(t0 + half, 1.0);
            if (s0 > s1) continue;
            #define EZ(s) (P[2] + (s) * ez - hd_prof(d, sqrt(d2 + ll * ((s) - t0) * ((s) - t0))))
            if (d->Rb <= 0.0) { double z = EZ(ez >= 0 ? s1 : s0); if (z > best) best = z; }
            else { double a = s0, c = s1, gr = 0.3819660112501051, m1 = a + gr * (c - a), m2 = c - gr * (c - a), f_1 = EZ(m1), f_2 = EZ(m2);
                for (int it = 0; it < 48 && c - a > 1e-12; it++) { if (f_1 < f_2) { a = m1; m1 = m2; f_1 = f_2; m2 = c - gr * (c - a); f_2 = EZ(m2); } else { c = m2; m2 = m1; f_2 = f_1; m1 = a + gr * (c - a); f_1 = EZ(m1); } }
                double z = fmax(fmax(f_1, f_2), fmax(EZ(s0), EZ(s1))); if (z > best) best = z; }
            #undef EZ
        }
    } } }
    #undef VERT
    return best;
}


#include "cam3d_checktrim.c"   /* Edit Passes to Surfaces 0x141f (needs hd_tip above) */

/* The Z window of a pass (FUN_180361bd0 raster/grid.c l.2266 with FUN_1802f3a80, EvaMachLocBasePlan): a position is in when its tip z + Eva +0x140 (here the tip itself) lies in [PosZLo, PosZHi] (+0x6c0 / +0x6c8) and,
   with a wider test, in [PosZLo - t, PosZHi + t] (+0x6d0 / +0x6d8, t = MachTrimZTolerance 0x1b39; FUN_1802f2340): positions are REMOVED, not clamped; a stretch within the wide window stays when one of its positions is in the tight one.
   The contact-layer test (+0x6e0, PosContactOnly) only concerns the tool's upper layers: no tool assembly here. The refinement of the end points (the second loop) is not applied: the ends are at the sampling step. */
static void hst_z_filter(const double *z, int n, double lo, double hi, double t, char *keep) {
    int i = 0;
    while (i < n) {
        if (!(z[i] >= lo - t && z[i] <= hi + t)) { keep[i++] = 0; continue; }
        int a = i, tight = 0;
        while (i < n && z[i] >= lo - t && z[i] <= hi + t) { if (z[i] >= lo && z[i] <= hi) tight = 1; i++; }
        for (int k = a; k < i; k++) keep[k] = (char)tight;
    }
}
/* ------------------------------------------------------------------------------------------------------------- Raster */
/* HST Raster (UI style 5, kernel style 4). Decode: formats/hst3d_port_plan.md 8 (G = decomp/raster/grid.c, mk = decomp/mckernel_all.c).
   Lines (FUN_18037ca90 G l.20060-20397): along u1 = (cos A, sin A), stepping along u2 = (sin A, -cos A) (RasterAngle = deg(A) + 180, A = record
   0x12f0 radians, mk l.88081-88087); over the ranges [min1, max1] / [min2, max2] of the measured objects, each line from min1 - (e + 1) to max1 +
   e + 1 (e = PassesExtension); q = (max2 - min2) / step, k = int(q), k + 1 lines at d_i = min2 + i step + (q - k) step / 2, i ascending, each A -> B.
   Links (FUN_180075b48 mk l.89806-89963 + the 0x142b defaults hl l.13493-13530): Zigzag (0x16bd) -> bidirectional; else one way, reversed for
   Other Way (0x16d5). Ours: which objects are measured (here the containment, else the drive's XY box - NOT DETERMINED); e = 0 (the record
   field of PassExtension is 0x12f8 = K+0x270 by FUN_180923f60, but the plan reads a parameter of another name, PassesExtension 0x1ad7); the tool positions are CToolDropPos on the triangles (hd_tip) (sampled at the height-field cell: FUN_1803612d0's step rule not applied, it made the oracle worse); off the drive the drive's lowest Z; the
   containment trimming (the line's inside intervals; the trimming rule is NOT DETERMINED); the SAFE links. */
typedef struct { const HstDrop *dp; double s_up; } RasterDrop;
static double raster_zdrop(void *ctx, double x, double y) { const RasterDrop *r = ctx; double z = hd_tip(r->dp, x, y); return z > -1e29 ? z + r->s_up : z; }
/* the drop for the linker's mm frame (N13-27): XY in mm -> op units, Z back to mm */
/* the linker's drop FUN_1801cf7b0 without the +0x88 region path = FUN_1801cf5e0 (x4 l.214-240): the layer drop FUN_1801cf500 on the drive (r), then,
   with a protect model (linker +0x7c0, its virtual +0xa0 "empty" false), FUN_1803a46d0 (x4 l.551-600): a CToolDropPos of the protect profile (+0x17a0
   at layer 0) on that model from z' - P (P = LinkerProtectThickness +0x1260), whose result h gives z' = h + P when h is above the start - so the node
   ends at max(z, drive + ToolThicknessZ, h + P). pd = NULL: no protect model (Raster without checks: 0 calls traced). */
typedef struct { const RasterDrop *r; double mf; const HstDrop *pd; double P; long np; } RasterDropMm;
static double raster_zdrop_mm(void *ctx, double x, double y) {
    RasterDropMm *m = ctx; double z = raster_zdrop((void *)m->r, x / m->mf, y / m->mf); if (z > -1e29) z *= m->mf;
    if (m->pd) { m->np++; double h = hd_tip(m->pd, x / m->mf, y / m->mf); if (h > -1e29) { double zp = (h + m->P) * m->mf; if (zp > z) z = zp; } }
    return z;
}
/* the passes between the passes plan and the linker: points in the pass direction, the frag key (the raster line) */
typedef struct { double *p; int n, key; int *pre, npre; } RPass;   /* pre: the prerequisites (rp_prereq) */
typedef struct { RPass *v; int n, cap, key; } RPassList;
static void rp_push(const double *xyz, int m, void *ctx) {
    RPassList *L = ctx; if (m < 2) return;
    if (L->n == L->cap) { L->cap = L->cap ? 2 * L->cap : 64; L->v = realloc(L->v, sizeof(RPass) * L->cap); }
    RPass *P = &L->v[L->n++]; memset(P, 0, sizeof *P); P->n = m; P->key = L->key; P->p = malloc(sizeof(double) * 3 * m); memcpy(P->p, xyz, sizeof(double) * 3 * m);
}
static void rp_free(RPassList *L) { for (int i = 0; i < L->n; i++) { free(L->v[i].p); free(L->v[i].pre); } free(L->v); memset(L, 0, sizeof *L); }
static void rp_reverse(RPass *P) { for (int a = 0, c = P->n - 1; a < c; a++, c--) for (int i = 0; i < 3; i++) { double t = P->p[3 * a + i]; P->p[3 * a + i] = P->p[3 * c + i]; P->p[3 * c + i] = t; } }
static void rp_write(const char *path, const RPassList *L) {
    FILE *o = fopen(path, "w"); if (!o) return;
    for (int i = 0; i < L->n; i++) { fprintf(o, "PASS %d\n", L->v[i].n); for (int k = 0; k < L->v[i].n; k++) fprintf(o, "%.9f %.9f %.9f\n", L->v[i].p[3 * k], L->v[i].p[3 * k + 1], L->v[i].p[3 * k + 2]); }
    fclose(o);
}
/* MCAM_HMM_PASSES (testing): "PASS n" + n lines "x y z"; a new key when the line offset d = p0 . u2 moves by more than a quarter step */
static int rp_read(const char *path, RPassList *L, double u2x, double u2y, double step, double sc) {
    FILE *f = fopen(path, "r"); if (!f) return 0;
    char ln[256]; int n = 0, key = -1; double dprev = 0; double *buf = NULL;
    while (fgets(ln, sizeof ln, f)) {
        if (sscanf(ln, "PASS %d", &n) != 1 || n < 1) continue;
        buf = realloc(buf, sizeof(double) * 3 * n);
        int k = 0; while (k < n && fgets(ln, sizeof ln, f) && sscanf(ln, "%lf %lf %lf", &buf[3 * k], &buf[3 * k + 1], &buf[3 * k + 2]) == 3) k++;
        if (k < n) break;
        for (int q = 0; q < 3 * n; q++) buf[q] *= sc;
        double d = buf[0] * u2x + buf[1] * u2y;
        if (key < 0 || fabs(d - dprev) > 0.25 * step) key++;
        dprev = d; L->key = key; rp_push(buf, n, L);
    }
    free(buf); fclose(f);
    return L->n > 0;
}
/* the prerequisites of a pass (FragOrderer's +0x340 lists, DECODED, hst_linker.md 19.1; hk = decomp/hmm_linker.c, x2 = hmm_linker_x2.c, G = raster/grid.c):
   - the graph: the grid plan gives every raster line its CTessToolpath (FUN_1802ed200 tb.c l.1086, node index i = the line's place in the grid's
     line vector, grid.c l.1578) and, through the plan's virtual +0x20 FUN_180013ab0 -> FUN_1802ed200's placeholder pair completed by FUN_1802ed2c0
     (proj/h2.c l.2202), the edge i -> i-1; the linker's graph FUN_180182e20 (x2 l.1600-1850) keeps the lines whose toolpath holds passes and walks
     over the others (FUN_180183b80 / FUN_180184200: a node without a key is passed through), so the neighbour is the nearest line holding passes;
   - FUN_1801ba430 / FUN_1801ba5e0 (hk l.28708-28870): for every pass P the list +0x110 = FUN_1801ba970: the passes of the neighbouring line in the
     direction LinkHeadNorthwards (+0xf40 <- +0x10d1 = 0x1a95, default !LinkReverseOrder, FUN_1803acd10 hl l.13590; mckernel writes it for Waterline
     only, mk l.90038; LinkORLocalNorth 0x1b16 false for Zigzag / one way, so no flip) = FUN_180184200, the predecessors = the NEXT line i+1,
     filtered (LinkORLimitRangeAdjacentLevel 0x1b12 true, FUN_1801c9bd0) by the overlap test FUN_1801c2940 -> FUN_1801c2300 below;
     FUN_1801c98a0 (x7 l.865-960) inverts +0x110 into +0x340: the prerequisites of a pass Q on line i+1 = the passes P on line i (the previous
     line holding passes in the plan's order) whose test with Q is true. FUN_1801bf440 -> FUN_1801bd390 -> FUN_1801be020 (hk l.31499, OrderingType 1,
     WholeLevelBeforeNext false): blocked while one of them is not taken (FUN_1801ca390 keeps the not taken ones);
   - the overlap test (FUN_1801c2300 hk l.34418, FUN_18027a8d0): A = P, B = Q, swapped when Q has fewer points (FUN_18027b290 counts the frag's
     points); d = LinkORAdjacentLevelOverlap 0x1b13 = EvaluateR(MachiningPasses, MachStepover) * 1.5 (hl l.13650-13665; d = 0 -> false); false when the
     XY boxes are farther apart than d (FUN_180062650); else for A's start, then A's end (frag +0x88 / +0xb0): q = the point of B nearest to it,
     a = the point of A nearest to q, b = the point of B nearest to a (FUN_18027a7a0), true when |a - b|_XY <= d (LinkORMeasureInUV 0x1b14 true: XY);
     neither -> false (LinkORAccurateClosestPair 0x1b15 false). The nearest point FUN_18027d4e0 / FUN_18027d940 (XY): per segment the interior
     foot (0 < t < 1 of the XY projection, the point interpolated in 3D), then the segment's end vertex, the first vertex once; the first of equal
     distances is kept (strict <). */
static int rp_npt(const double *p, int n, const double *q, double *out) {
    double bd = -1;
    for (int i = 0; i + 1 < n; i++) {
        const double *a = p + 3 * i, *b = a + 3; double dx = b[0] - a[0], dy = b[1] - a[1], L = dx * dx + dy * dy;
        double t = L == 0 ? 0.5 : ((q[0] - a[0]) * dx + (q[1] - a[1]) * dy) / L;
        if (t > 0 && t < 1) { double f[3]; for (int k = 0; k < 3; k++) f[k] = a[k] + t * (b[k] - a[k]);
            double d = (f[0] - q[0]) * (f[0] - q[0]) + (f[1] - q[1]) * (f[1] - q[1]); if (bd < 0 || d < bd) { bd = d; memcpy(out, f, sizeof f); } }
        double d = (b[0] - q[0]) * (b[0] - q[0]) + (b[1] - q[1]) * (b[1] - q[1]); if (bd < 0 || d < bd) { bd = d; memcpy(out, b, 3 * sizeof *b); }
        if (i == 0) { d = (a[0] - q[0]) * (a[0] - q[0]) + (a[1] - q[1]) * (a[1] - q[1]); if (bd < 0 || d < bd) { bd = d; memcpy(out, a, 3 * sizeof *a); } }
    }
    if (bd < 0) memcpy(out, p, 3 * sizeof *p);
    return 1;
}
static int rp_overlap(const RPass *A, const RPass *B, double d) {
    if (B->n < A->n) { const RPass *t = A; A = B; B = t; }
    double d2 = d * d; if (d2 == 0) return 0;
    double ba[4] = { 1e300, -1e300, 1e300, -1e300 }, bb[4] = { 1e300, -1e300, 1e300, -1e300 };
    for (int q = 0; q < A->n; q++) { ba[0] = fmin(ba[0], A->p[3 * q]); ba[1] = fmax(ba[1], A->p[3 * q]); ba[2] = fmin(ba[2], A->p[3 * q + 1]); ba[3] = fmax(ba[3], A->p[3 * q + 1]); }
    for (int q = 0; q < B->n; q++) { bb[0] = fmin(bb[0], B->p[3 * q]); bb[1] = fmax(bb[1], B->p[3 * q]); bb[2] = fmin(bb[2], B->p[3 * q + 1]); bb[3] = fmax(bb[3], B->p[3 * q + 1]); }
    double gx = fmax(0.0, fmax(bb[0] - ba[1], ba[0] - bb[1])), gy = fmax(0.0, fmax(bb[2] - ba[3], ba[2] - bb[3]));
    if (gx * gx + gy * gy > d2) return 0;
    for (int e = 0; e < 2; e++) {
        double q0[3], a[3], b[3];
        rp_npt(B->p, B->n, e ? A->p + 3 * (A->n - 1) : A->p, q0); rp_npt(A->p, A->n, q0, a); rp_npt(B->p, B->n, a, b);
        if ((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) <= d2) return 1;
    }
    return 0;
}
static void rp_prereq(RPassList *L, double d) {
    for (int s = 0, ps = -1, pt = -1; s < L->n; ) {
        int t = s; while (t < L->n && L->v[t].key == L->v[s].key) t++;
        for (int j = s; j < t; j++) { RPass *Q = &L->v[j];
            for (int i = ps; i >= 0 && i < pt; i++) if (rp_overlap(&L->v[i], Q, d)) {
                Q->pre = realloc(Q->pre, sizeof(int) * (Q->npre + 1)); Q->pre[Q->npre++] = i; } }
        ps = s; pt = t; s = t;
    }
}
/* FUN_1801b9820 (hk l.28139-28370) over the passes not yet taken whose prerequisites are all taken (FUN_1801bf440 / FUN_1801be020; when none is
   free, FUN_1801b90d0's last step (4) takes every pass not yet taken), in their direction after FUN_1803a2750; the score FUN_18027a4f0 = the
   squared 3D distance from the point to the pass's start (hst_linker.md 14.4); equal scores -> the smaller entity key (the map insert keeps the
   first; the key order [inferred] = the plan's order):
   - with a previous pass (orderer +0xf08): FUN_1801b9cc0 / FUN_1801b9d70: the start nearest the previous pass's end;
   - without (the first pass): N = the start nearest the start point (LinkStartX, LinkStartY, LinkSafeZ; FUN_1803a3ae0, Top plane); then the candidates
     in score order after N: while the XY distance from N's start to the candidate's END is <= LinkMaxStayOnSurface (+0x1118) (hk l.28245-28258; a
     closed candidate stops the walk; the kind-0 neighbour test does not apply to Raster) the candidate becomes the choice - the last one wins. */
typedef struct { double d; int j; } RpSc;
static int rp_sc_cmp(const void *a, const void *b) { const RpSc *x = a, *y = b; return x->d < y->d ? -1 : x->d > y->d ? 1 : x->j - y->j; }
static int rp_free_pass(const RPassList *L, const char *used, int j, int any) {
    if (used[j]) return 0;
    if (any) return 1;
    for (int k = 0; k < L->v[j].npre; k++) if (!used[L->v[j].pre[k]]) return 0;
    return 1;
}
static int rp_next(const RPassList *L, const char *used, const double *pt, int have, double maxstay) {
    int any = 1; for (int j = 0; j < L->n && any; j++) if (rp_free_pass(L, used, j, 0)) any = 0;
    int n = 0; RpSc *ix = malloc(sizeof(RpSc) * (L->n ? L->n : 1));
    for (int j = 0; j < L->n; j++) { if (!rp_free_pass(L, used, j, any)) continue; const double *s = L->v[j].p;
        ix[n].d = (s[0] - pt[0]) * (s[0] - pt[0]) + (s[1] - pt[1]) * (s[1] - pt[1]) + (s[2] - pt[2]) * (s[2] - pt[2]); ix[n++].j = j; }
    if (!n) { free(ix); return -1; }
    qsort(ix, n, sizeof *ix, rp_sc_cmp);
    int choice = ix[0].j;
    if (!have) {
        const double *ns = L->v[ix[0].j].p;
        for (int a = 1; a < n; a++) {
            const RPass *C = &L->v[ix[a].j]; const double *ce = C->p + 3 * (C->n - 1);
            if (sqrt((ns[0] - ce[0]) * (ns[0] - ce[0]) + (ns[1] - ce[1]) * (ns[1] - ce[1])) > maxstay) break;
            choice = ix[a].j;
        }
    }
    free(ix);
    return choice;
}
/* the order and emission of the collected passes (W in mm; u = mm per op unit): the decoded order when !fail, else the old order (ours: the collection
   order, a closed pass from its vertex nearest the tool); the passes are joined by the SAFE rule (c3_emit_run, ours) */
/* The Waterline surface ramp between two closed passes (link kind 3; FUN_1801c3dd0 x1 l.1919-2330, FUN_1801d2110 x3 l.2830-3200, FUN_1801dd430 hlc l.32-180).
   FUN_1801c3dd0, plan kind 0 (Waterline), SurfaceRampStyle 1 (+0x1200), the next pass N closed (frag +0x78): d = |zN - zP| / t with t = sqrt((1 - c^2) / c^2),
   c = cos(SurfaceRampAngle) (+0x1210); N (already re-split at S, its point nearest the previous end, by FUN_1801c05c0) is re-split d further along itself
   (FUN_180279f90 + FUN_180281580): N runs from S + d round to S + d. The link struct gets two pieces: +8 = P's extension, the part of P from its end d
   further along P (FUN_180279f90 from P's end, FUN_1802829c0; a closed P wraps), when d < P's length (FUN_18027a4b0); +0x10 = N's ramp piece, from S
   (FUN_180279f90 by -d from N's new start) to N's new start, when 0 < d < N's length. FUN_180199220 then builds the connect (FUN_1801a8fe0 -> FUN_1801d1e70
   mode 3 -> FUN_1801d2110, which keeps the pieces at +0x118 / +0x128) from P's end to N's new start and reduces it (FUN_1801d2bd0, c3_sd_split mode 3) on
   the connect's curve FUN_1801dd430: with a piece A(t) = the point at t x its length along it (FUN_18027a300: the start at 0, the end at 1, else
   FUN_180279f90 by t L), B(t) likewise on N's piece; s = t, or 3 t^2 - 2 t^3 (t t 2 (1.5 - t)) with the connect's +0x68 = LinkSurfaceRampSmooth
   (+0x1120: Transitions Tangential, mk l.90090-90128); the point = A (1 - s) + B s (A itself when the two are equal). The rise test (+0x38 above
   SurfaceRampMaxVerticalChange, the Waterline default 1e6, hl l.18965-18975, not written by mckernel) does not trigger; the nodes are the link at the
   feed (FUN_180192510). The connect wanders (c3_sd_wander / c3_sd_wcheck); an open P whose toolpath item is closed
   ramps along that item (wl_link, FUN_18027b580). Checked node by node against mchmm.dll (MCAM_SD_DEBUG vs HMM_TRACE 1801d2bd0:l, formats/hst_linker.md
   25, 26: SWARF op 4 22 / 22, MOLD BOTTLE op 4 27 / 27 closed -> closed). The DLL's output
   stage then merges collinear moves and fits arcs (the toolpath writer FUN_1801186d0, its tolerance 0.0127 mm / cos 45 deg: NOT PORTED, the nodes are
   written as they are). */
struct WlRamp { double *a, *b; int na, nb; double la, lb; int smooth; };
/* the polyline p (n points, mm) from arc length s0 to s1 (0 <= s0 < s1 <= its length); its length in *len */
static int wl_sub(const double *p, int n, double s0, double s1, double **out, double *len) {
    double *o = malloc(sizeof(double) * 3 * (n + 2)); int m = 0; double acc = 0, L = 0;
    for (int i = 0; i + 1 < n; i++) {
        const double *a = p + 3 * i, *c = a + 3; double sl = sqrt(wl_d3(a, c));
        if (!(sl > 0)) continue;
        double e0 = acc, e1 = acc + sl; acc = e1;
        if (e1 < s0 || e0 > s1) continue;
        if (!m) { double t = (s0 - e0) / sl; t = t < 0 ? 0 : t; for (int k = 0; k < 3; k++) o[k] = a[k] + (c[k] - a[k]) * t; m = 1; }
        if (e1 <= s1) { memcpy(o + 3 * m, c, sizeof(double) * 3); m++; }
        else { double t = (s1 - e0) / sl; for (int k = 0; k < 3; k++) o[3 * m + k] = a[k] + (c[k] - a[k]) * t; m++; break; }
    }
    for (int i = 0; i + 1 < m; i++) L += sqrt(wl_d3(o + 3 * i, o + 3 * i + 3));
    *out = o; *len = L; return m;
}
/* FUN_18027a300: the point at t x length along the piece */
static void wl_at(const double *p, int n, double L, double t, double *o) {
    if (t == 0.0) { memcpy(o, p, sizeof(double) * 3); return; }
    if (t == 1.0) { memcpy(o, p + 3 * (n - 1), sizeof(double) * 3); return; }
    double rem = t * L;
    for (int i = 0; i + 1 < n; i++) {
        const double *a = p + 3 * i, *c = a + 3; double sl = sqrt(wl_d3(a, c));
        if (sl >= rem) { for (int k = 0; k < 3; k++) o[k] = a[k] + (c[k] - a[k]) * (sl > 0 ? rem / sl : 0.0); return; }
        rem -= sl;
    }
    memcpy(o, p + 3 * (n - 1), sizeof(double) * 3);
}
static void wl_ramp_pt(const WlRamp *w, double t, double *o) {
    double A[3], B[3]; wl_at(w->a, w->na, w->la, t, A); wl_at(w->b, w->nb, w->lb, t, B);
    if (w->smooth) t = t * t * 2.0 * (1.5 - t);
    if (A[0] == B[0] && A[1] == B[1] && A[2] == B[2]) { memcpy(o, A, sizeof A); return; }
    double r = 1.0 - t; for (int k = 0; k < 3; k++) o[k] = B[k] * t + A[k] * r;
}
/* The connect's wander pass for a connect between two closed frags (FUN_1801d2bd0 x1 l.1282-1290: the wander flag +0xa0 = both frags' +0x78, set in
   FUN_1801d2110; FUN_1801deaf0 x3 l.1065-1207 returns 0 for 5 or more plain nodes, so FUN_1801dde40 x3 l.1209-1380 runs), for nodes without arcs (flags
   2 / 8) and all flagged 0x40 (FUN_1801d2bd0 marks every reduce node with the virtual +0x20 case 8): over the node triples (a, b, c) FUN_1801e07f0 (x5
   l.1046-1233) keeps b when it lies above the line a-c at b (z_i = the Z of a and c weighted by the XY lengths |ab| / (|ab| + |bc|); c's Z when |bc| = 0,
   a's when |ab| = 0 or both Z equal; false when |ac| = 0); else FUN_1801de920 (x5 l.965-1045): z_i <= b.z -> the next triple (FUN_1801ce930), else b
   is flagged 0x80 (virtual +0x20 case 9, FUN_1801dcff0 hlc l.248) and the walk steps back (b = a, a = the kept node before it; from the first node: b =
   c, c = the next). FUN_1801de1f0 (x5 l.785-865) then puts every run of flagged nodes back on the line between its kept neighbours (FUN_1801e0d70,
   decomp/hmm_linker_x13.c l.1-88: the Z at the run node's XY length fraction, the node raised only) - the nodes' upper hull in (XY length, Z). */
static void wl_wander(double *p, int n) {
    if (n < 5) return;                                                         /* FUN_1801deaf0: 2 - 4 nodes (plain) are left alone */
    unsigned char *fl = malloc((size_t)n); memset(fl, 0x40, (size_t)n);       /* every node 0x40 (virtual +0x20 case 8) */
    hl_wander(p, n, fl);                                                       /* FUN_1801dde40 (cam3d_hstlink.c, shared with the Raster linker) */
    free(fl);
}
static double wl_len(const double *p, int n) { double L = 0; for (int i = 0; i + 1 < n; i++) L += sqrt(wl_d3(p + 3 * i, p + 3 * i + 3)); return L; }
/* the ramp from the closed pass P (its emitted points pp / np, mm) to the closed pass N (its emitted points, starting at S + d) over d; emitted at the feed
   in op units (1 / u); 1 = emitted, 0 = not decoded (SAFE) */
static int wl_ramp_link(C3StayDown *sd, MB *b, const double *pp, int np, const double *pn, int nnp, double d, int smooth, double u, double feed, double wz0, int hull) {
    double LP = wl_len(pp, np), LN = wl_len(pn, nnp);
    if (!(d > 0) || !(d < LP) || !(d < LN)) return 0;                         /* the whole-frag piece branches (FUN_1801c3dd0 l.2240-2290): not checked - SAFE */
    WlRamp w; w.smooth = smooth;
    w.na = wl_sub(pp, np, 0.0, d, &w.a, &w.la); w.nb = wl_sub(pn, nnp, LN - d, LN, &w.b, &w.lb);
    if (w.na < 2 || w.nb < 2) { free(w.a); free(w.b); return 0; }
    memcpy(w.b + 3 * (w.nb - 1), pn, sizeof(double) * 3);                   /* N's piece ends at N's start (the closed loop's both ends) */
    /* the node wander (c3_sd_wander, c3_sd_wcheck): the Waterline connect wanders (+0xb0 = LinkAllowWandering: FUN_1801d4990 runs on every new node
       of op 4's ramps, traced 486 calls; the Waterline plan's default is not in decomp/, mckernel's macro does not write it), +0xb8 =
       LinkWanderInitialOffset 0x1b2d = max(0.006, StepMachTolerance x 1.1) mm (hl l.12286-12292), +0x80 = the linker emitter's +0x38 (wz0) */
    C3Sd s; memset(&s, 0, sizeof s); s.mode = 3; s.sd = sd; s.wr = &w; s.dbg = getenv("MCAM_SD_DEBUG") != NULL;
    s.wnd = 1; s.woff = fmax(0.006, sd->cn->smt * 1.1); s.wz0 = wz0;
    const double *P0 = pp + 3 * (np - 1), *P1 = pn;
    double N0[7] = { P0[0], P0[1], P0[2], 0, 0, 0, 0 }, N1[7] = { P1[0], P1[1], P1[2], 0, 0, 0, 0 };
    if (s.dbg) fprintf(stderr, "sd link 3 (%.17g %.17g %.17g) (%.17g %.17g %.17g) d %.17g\n", P0[0], P0[1], P0[2], P1[0], P1[1], P1[2], d);
    c3_sd_add(&s, 0.0f, N0); c3_sd_split(&s, 0.0f, N0, 1.0f, N1, 0);
    c3_sd_wcheck(&s);                                                         /* mode 3: FUN_1801d3a60 / 1801d4270 / 1801d3c20 / 1801d39e0 */
    if (hull) wl_wander(s.p, s.n);                                            /* both frags closed (the connect's +0xa0, FUN_1801d2110): the wander pass */
    int ok = !(s.maxrise > sd->maxvert);
    if (s.lift) sd->nrise++;
    if (s.dbg) for (int k = 0; k < s.n; k++) fprintf(stderr, "sd node %d %.17g %.17g %.17g t %.9g\n", k, s.p[3 * k], s.p[3 * k + 1], s.p[3 * k + 2], (double)s.t[k]);
    if (ok) for (int k = 1; k < s.n; k++) mv(b, 0, s.p[3 * k] / u, s.p[3 * k + 1] / u, s.p[3 * k + 2] / u, feed);
    free(s.t); free(s.p); free(s.w); free(w.a); free(w.b);
    if (ok) sd->nstay++; else sd->nsafe++;
    return ok;
}
/* ---- The Waterline retract with horizontal leads (FUN_1801a1d70 / FUN_180194140 -> FUN_18019e010; formats/hst_linker.md 29, 30). mm (N13-27).
   hk = decomp/hmm_linker.c, x17 = decomp/hmm_linker_x17.c; "dis" = the disassembly of mchmm.dll; traced with tools/peloader HMM_TRACE (:n block
   coverage) on TUTORIAL 2 SWARF op 4: the first approach and retracts 1-14 take the path ported here ("clean"), every other branch keeps the SAFE
   link (ours, labelled; user decision N13-29). */
/* the unit-free tangent of a pass at a point q0 = p, walking along it by st doubles (3: forward from the start, -3: backward from the end, then negated),
   na points available - FUN_18027ad40's derivative: a line frag's direction (q1 - q0); with MCAM_HMM_PASSES (testing: the DLL's passes, their arcs
   written as equal chords) the tangent of the circle through q1, q2, q3 when q0 lies on it too (the arc the chords came from) */
static void wl_tan(const double *p, int st, int na, int end, double *t) {
    const double *q0 = p, *q1 = p + st;
    t[0] = q1[0] - q0[0]; t[1] = q1[1] - q0[1]; t[2] = q1[2] - q0[2];
    if (wl_arctan && na >= 4) {
        const double *q2 = p + 2 * st, *q3 = p + 3 * st;
        double bx = q2[0] - q1[0], by = q2[1] - q1[1], cx = q3[0] - q1[0], cy = q3[1] - q1[1], d = 2.0 * (bx * cy - by * cx);
        if (fabs(d) > 1e-12 * (bx * bx + by * by + cx * cx + cy * cy)) {
            double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy, ox = q1[0] + (cy * b2 - by * c2) / d, oy = q1[1] + (bx * c2 - cx * b2) / d;
            double r = hypot(q1[0] - ox, q1[1] - oy), r0 = hypot(q0[0] - ox, q0[1] - oy);
            if (fabs(r0 - r) <= 1e-7 * r) {
                double tx = -(q0[1] - oy), ty = q0[0] - ox;                  /* the radius at q0 turned by 90 deg, the way to q1 */
                if (tx * t[0] + ty * t[1] < 0) { tx = -tx; ty = -ty; }
                t[0] = tx; t[1] = ty; t[2] = 0.0;
            }
        }
    }
    if (end) { t[0] = -t[0]; t[1] = -t[1]; t[2] = -t[2]; }
}
/* FUN_18004d7e0 (dis 18004d7e0-18004d87e): the "diamond" angle of (x, y) in [0, 4) (4 = 0) */
static double wl_dia(double x, double y) {
    if (x == 0.0 && y == 0.0) return 0.0;
    if (y >= 0.0) return x >= 0.0 ? y / (x + y) : 1.0 - x / (y - x);
    if (x < 0.0) return 2.0 - y / (-x - y);
    double r = x / (x - y) + 3.0; return r == 4.0 ? 0.0 : r;
}
static double wl_ang(double y, double x) { double a = atan2(y, x), tp = 2.0 * M_PI; if (a < 0.0) a += tp; if (tp <= a) a -= tp; return a; }
/* FUN_180051d20 (dis 180051d20-180051dfa): the sweep of the arc A -> B around C (side 0 counterclockwise), [0, 2 pi); FUN_180051e00 (dis 180051e00-
   180051f33): the point at t of that sweep, C + u cos w + p sin w (u = A - C, p = u turned by 90 deg the arc's way) */
static double wl_sweep(const double *C, const double *A, const double *B, int side) {
    double ux = A[0] - C[0], uy = A[1] - C[1], px = side ? uy : -uy, py = side ? -ux : ux, vx = B[0] - C[0], vy = B[1] - C[1];
    return wl_ang(vy * py + vx * px, vy * uy + ux * vx);
}
static void wl_arcpt(const double *C, const double *A, const double *B, int side, double t, double *o) {
    double ux = A[0] - C[0], uy = A[1] - C[1], px = side ? uy : -uy, py = side ? -ux : ux, w = wl_sweep(C, A, B, side) * t, s = sin(w), c = cos(w);
    o[0] = C[0] + ux * c + px * s; o[1] = C[1] + uy * c + py * s;
}
/* FUN_1802887a0 (decomp/hst3d_trimsurf_meas.c l.1-45, MeasureInterior2D): min(1000, the distance to the set's curves), negative when the crossing
   count is even (outside); the set = the region's loops (WlPass.rg) */
static double wl_meas(const WlPass *P, double x, double y) {
    double best = 1000.0 * 1000.0; int in = 0; const double *q = P->rg;
    for (int k = 0; k < P->nrg; q += 2 * P->rgn[k], k++) {
        int n = P->rgn[k];
        for (int i = 0, j = n - 1; i < n; j = i++) {
            double xi = q[2 * i], yi = q[2 * i + 1], xj = q[2 * j], yj = q[2 * j + 1];
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
            double A[3] = { xj, yj, 0 }, B[3] = { xi, yi, 0 }, Q[3] = { x, y, 0 }, d2 = c3_seg_d2(Q, A, B);
            if (d2 < best) best = d2;
        }
    }
    double d = sqrt(best); return in ? d : -d;
}
/* FUN_180176540 (x17 l.170-238; its arguments from dis 1801a194c-1801a1984): the lead circle of radius R at the pass point P (unit tangent T) on the
   left (side 0) when it fits in the region with 0.012 to spare, else on the right, else the one with the larger value (ties to the right) */
static void wl_side(const WlPass *Pp, const double *P, const double *T, double R, double *C, int *side) {
    double d = sqrt(T[0] * T[0] + -T[1] * -T[1]), dd = d == 0.0 ? 1.0 : d;
    double c0[2] = { (1.0 / dd) * -T[1] * R + P[0], (1.0 / dd) * T[0] * R + P[1] };
    double m0 = wl_meas(Pp, c0[0], c0[1]);
    if (R - 0.012 <= m0) { C[0] = c0[0]; C[1] = c0[1]; *side = 0; return; }
    double c1[2] = { P[0] - (1.0 / dd) * -T[1] * R, P[1] - (1.0 / dd) * T[0] * R };
    double m1 = wl_meas(Pp, c1[0], c1[1]);
    if (R - 0.012 <= m1 || m0 <= m1) { C[0] = c1[0]; C[1] = c1[1]; *side = 1; return; }
    C[0] = c0[0]; C[1] = c0[1]; *side = 0;
}
/* FUN_18019b180 (hk l.7200-7330): the tangent point on the circle (C, R) seen from Q, of the two the one on the side's way from P (P = the pass end) */
static void wl_tangpt(const double *Q, const double *P, const double *C, double R, int side, double *o) {
    if (R == 0.0) { o[0] = P[0]; o[1] = P[1]; return; }
    double q0 = Q[0], q1 = Q[1], dy = C[1] - q1, dx = C[0] - q0, l2 = dy * dy + dx * dx;
    if (l2 <= R * R) { o[0] = P[0]; o[1] = P[1]; return; }
    double L = sqrt(l2 - R * R);
    double ax = -R * dy + dx * L, ay = R * dx + dy * L, an = sqrt(ay * ay + ax * ax); if (an == 0.0) an = 1.0;
    double bx = dy * R + dx * L, by = -R * dx + dy * L, bn = sqrt(by * by + bx * bx); if (bn == 0.0) bn = 1.0;
    double t1[2] = { ax * (1.0 / an) * L + q0, ay * (1.0 / an) * L + q1 }, t2[2] = { bx * (1.0 / bn) * L + q0, by * (1.0 / bn) * L + q1 };
    double px = P[0] - C[0], py = P[1] - C[1], e1x = t1[0] - C[0], e1y = t1[1] - C[1], e2x = t2[0] - C[0], e2y = t2[1] - C[1];
    int b6 = !((e1y * px + -py * e1x < 0.0) == (side != 0)) || (t1[0] == P[0] && t1[1] == P[1]);
    int b7 = !((e2y * px + -py * e2x < 0.0) == (side != 0)) || (t2[0] == P[0] && t2[1] == P[1]);
    const double *r;
    if (b6) r = b7 ? ((e1y * py + e1x * px <= e2y * py + e2x * px) ? t1 : t2) : t2;
    else r = b7 ? t1 : ((e1y * py + e1x * px <= e2y * py + e2x * px) ? t2 : t1);
    o[0] = r[0]; o[1] = r[1];
}
/* FUN_18019afc0 (hk l.7143-7195): the helix start on the lead-in circle (C, R) above S: n full turns, the rest angle back from S the side's way */
static void wl_hstart(double tana, double top, const double *C, const double *S, double zb, double R, int side, int *n, double *H) {
    if (tana == 0.0) { *n = 0; H[0] = S[0]; H[1] = S[1]; return; }
    double tp = 2.0 * M_PI, L = (top - zb) / tana, turn = tp * R; int k = (int)(L / turn); *n = k;
    double rest = (L - (double)k * turn) / R, a = wl_ang(S[1] - C[1], S[0] - C[0]);
    if (!side) rest = -rest;
    a = rest + a; if (0.0 <= a) { if (tp < a) a -= tp; } else a += tp;
    H[0] = cos(a) * R + C[0]; H[1] = sin(a) * R + C[1];
}
/* FUN_1801d3530 (x17 l.885-1095) with FUN_1801cece0's corner branch (x10 l.1-209) and FUN_1801cd2f0 (x5 l.2688ff; t = 0.5 at dis 1801cd3de): the
   arc a -> b around C (C a corner node, flag 0x08, + 0x10 for side 1: traced) subdivided at its half-sweep point (the quarter point when the sweep is
   over 1.9 diamond units) until the diamond angle at C is <= 0.2 and FUN_1801ce270(a, b) is clear; every new point dropped (FUN_1801cf7b0) at the
   mean Z of its neighbours; 1 = a point rose (the connect's lifted flag +0xc8, read back as the check's result) */
static int wl_arcchk(C3Link *lk, const double *a, const double *C, const double *b, int side, int depth) {
    double ux = a[0] - C[0], uy = a[1] - C[1], px = side ? uy : -uy, py = side ? -ux : ux, vx = b[0] - C[0], vy = b[1] - C[1];
    double dot = vy * uy + vx * ux, cr = vy * py + vx * px;
    int sub;
    if ((dot == 0.0 && cr == 0.0) || wl_dia(dot, cr) <= 0.2) sub = c3_lc_chord(lk, a, b);   /* (wl_dia gives 0 for 4.0, as the inline test) */
    else sub = 1;
    if (!sub) return 0;
    if (depth > 60) return 1;                                                 /* (not reached: the chord test stops at StepMinHorizontalChange) */
    double M[3];
    if (wl_dia(dot, cr) <= 1.9) wl_arcpt(C, a, b, side, 0.5, M);
    else { M[0] = C[0] + px; M[1] = C[1] + py; }
    M[2] = a[2] == b[2] ? a[2] : (b[2] + a[2]) * 0.5;
    double z0 = M[2]; int up = c3_lc_drop(lk, M);
    if (lk && getenv("MCAM_WL_LEADDBG")) fprintf(stderr, "wl chk drop %.17g %.17g z0 %.17g -> %.17g %d\n", M[0], M[1], z0, M[2], up);
    if (up) return 1;
    if (wl_arcchk(lk, a, C, M, side, depth + 1)) return 1;
    return wl_arcchk(lk, M, C, b, side, depth + 1);
}
/* the drop of a point from a connect's base (FUN_1801d2940 / FUN_1801cf7b0 with the node at the connect's +0xa8): max(base, the layer-0 drop). The
   base of the check connects is min(the pass Z(s), *(emitter[0] + 0x450)) (dis 18019bc94-18019bca9, 18019c8b0-18019c8d7; -1.905 on op 4): every
   comparison below is against a Z at or above that base, so it equals "the drop is above the Z" whatever the base */
static double wl_dropz(C3Link *lk, double x, double y, double base) { lk->ndrop++; double z = lk->zdrop(lk->zctx, x, y); return z > base ? z : base; }
/* the arc writer's points (FUN_18019b590 hk l.7333-7446 / FUN_18011a350 decomp/hst3d_weave_drop.c l.6537-6700 / FUN_1801195f0): the arc A -> B around
   C over the sweep sw (the side's way), Z linear in the angle; chords at mccore's step (FUN_1809187f0) for the moves, tagged as the arc record */
static void wl_arcmv(MB *b, const double *C, const double *A, double za, double sw, int side, const double *B, double zb, double r, double atol, double u, double feed) {
    double st = r > atol ? acos((r - atol) / r) : M_PI; int n = st > 0 ? (int)ceil(sw / st) : 1; if (n < 1) n = 1;
    double a0 = atan2(A[1] - C[1], A[0] - C[0]), sg = side ? -1.0 : 1.0; int from = b->n;
    for (int k = 1; k <= n; k++) {
        if (k == n) { mv(b, 0, B[0] / u, B[1] / u, zb / u, feed); break; }
        double t = (double)k / n, a = a0 + sg * sw * t;
        mv(b, 0, (C[0] + r * cos(a)) / u, (C[1] + r * sin(a)) / u, (za + (zb - za) * t) / u, feed);
    }
    mb_tag_arc(b, from, C[0] / u, C[1] / u, side != 0);
}
/* a vertical lead arc P0 -> P1 around C (both radii |P - C|, at most a half turn), as mccore linearises it (FUN_1809187f0, c3_lk_arc) */
static void wl_varc(MB *b, const double *C, const double *P0, const double *P1, double atol, double u, double feed) {
    double a[3], w[3], r = 0, r1 = 0; for (int i = 0; i < 3; i++) { a[i] = P0[i] - C[i]; w[i] = P1[i] - C[i]; r += a[i] * a[i]; r1 += w[i] * w[i]; }
    r = sqrt(r); r1 = sqrt(r1); if (!(r > 0) || !(r1 > 0)) { mv(b, 0, P1[0] / u, P1[1] / u, P1[2] / u, feed); return; }
    double c = 0; for (int i = 0; i < 3; i++) { a[i] /= r; w[i] /= r1; c += a[i] * w[i]; } c = c > 1 ? 1 : c < -1 ? -1 : c;
    double sw = acos(c), s = sin(sw), v[3]; for (int i = 0; i < 3; i++) v[i] = s > 0 ? (w[i] - a[i] * c) / s : 0.0;
    double st = acos((r - atol) / r); int n = st > 0 ? (int)ceil(sw / st) : 1; if (n < 1) n = 1;
    for (int k = 1; k <= n; k++) {
        if (k == n) { mv(b, 0, P1[0] / u, P1[1] / u, P1[2] / u, feed); break; }
        double t = sw * k / n, ct = cos(t), sn = sin(t);
        mv(b, 0, (C[0] + r * (ct * a[0] + sn * v[0])) / u, (C[1] + r * (ct * a[1] + sn * v[1])) / u, (C[2] + r * (ct * a[2] + sn * v[2])) / u, feed);
    }
}
/* FUN_1801af3d0 (hk l.20796-20895) for a Waterline frag (toolpath type not 0x13fa): the toolpath's set in the map linker +0x430 +0x70: empty ->
   (float)RampHeightOffset + plan +0x1240 + the frag's start Z; else the first entry's toolpath Z (+0x88) against the frag's (+0xb0), the larger.
   The map's writer is NOT DECODED; traced on TUTORIAL 2 SWARF op 4 (every call, HMM_TRACE 1801af3d0): it is empty for the regions whose parent region
   (FUN_180400db0's graph) holds no pass (the top level's regions under the pass-less first level: 51.399 = 3 + 1.27 + 47.129) and gives the parent
   region's level otherwise (47.129 for a pass at 45.869) - so here: the parents holding passes, the lowest node (the key order) first. 0 = unknown */
static int wl_vtop(const WlLead *L, const WlPass *P, const WlNode *N, int nn, const int *npass, double *v) {
    double z = P->p[2];
    if (P->node < 0 || P->node >= nn) return 0;
    const WlNode *R = &N[P->node]; int q = -1;
    for (int t = 0; t < R->npar; t++) { int a = R->par[t]; if (a >= 0 && a < nn && npass[a] && (q < 0 || a < q)) q = a; }
    if (q < 0) { *v = (double)L->rho + L->sdown + z; return 1; }
    if (isnan(N[q].z)) return 0;
    *v = z <= N[q].z ? N[q].z : z; return 1;
}
/* The retry step of the lead checks FUN_18019bba0 / FUN_18019c7c0 (hk l.7682-7700 / 8177-8195): when the check is blocked the pass end (start) is
   moved along the pass by min(0.1, 0.1 LinkMaxAllowedTrimming) and the lead circle taken anew (FUN_180279f90, FUN_1801a1240) - unless the frag is
   closed (+0x78) or plan +0x110b (LinkTrimmingStyle 1) is clear and LinkTrimmingStyle (0x1b1c) is not 2: then the step is 0, one retry at the same
   point gives the same circle (FUN_1800082c0 on the new point, hk l.7955-7960) and the check stays blocked with X (H) put back (hk l.7962-7975).
   1 = the step is 0 (decoded), 0 = a nonzero step (the walk along the pass: NOT PORTED) */
static int wl_step0(const WlLead *L, const WlPass *P) { return P->closed || L->tstyle == 0; }
/* the lead-out side (FUN_18019bba0 hk l.7534ff, the path of dis 18019bc33-18019c7b0 traced on op 4): 1 = clear, 0 = blocked (*param_13 = 1:
   LAB_18019c328 or the check connect's lifted flag, hk l.7900-7935), -1 = NOT DECODED */
static int wl_chk_out(WlLead *L, const double *E, double zE, const double *C, int side, const double *X, double zb) {
    C3Link *lk = L->lk;
    double ex = E[0] - C[0], ey = E[1] - C[1], xx = X[0] - C[0], xy = X[1] - C[1];
    int split = !((ex * -xy + xx * ey > 0.0) == (side != 0)) || (X[0] == E[0] && X[1] == E[1]);
    if (!split) { double px = side ? ey : -ey, py = side ? -ex : ex; split = wl_dia(xx * ex + xy * ey, xy * py + xx * px) > 2.0; }
    double base = zE;                                                          /* (min with *(emitter[0] + 0x450): see wl_dropz) */
    if (!split) {                                                              /* dis 18019c308-18019c3a2 */
        if (wl_dropz(lk, X[0], X[1], base) > zb) return 0;                    /* FUN_1801d2940 at X above the lead Z: dis 18019c326 -> LAB_18019c328 */
        double A[3] = { E[0], E[1], zE }, Cc[3] = { C[0], C[1], zE }, B[3] = { X[0], X[1], zb };
        return !wl_arcchk(lk, A, Cc, B, side, 0);
    }
    /* over a half turn (dis 18019bfc2-18019c303): the arc's half-sweep point M (FUN_180051e00 t 0.5; a zero sweep: dis 18019c021, NOT DECODED) */
    if (wl_sweep(C, E, X, side) == 0.0) return -1;
    double M[3]; wl_arcpt(C, E, X, side, 0.5, M);
    double zm = (zb - zE) * 0.5 + zE; M[2] = zm;
    if (wl_dropz(lk, M[0], M[1], base) > zm) return 0;                         /* dis 18019c16b -> 18019c328 */
    if (wl_dropz(lk, X[0], X[1], base) > zb) return 0;                         /* dis 18019c222 -> 18019c328 */
    double A[3] = { E[0], E[1], zE }, C1[3] = { C[0], C[1], zE }, C2[3] = { C[0], C[1], zm }, B[3] = { X[0], X[1], zb };
    if (wl_arcchk(lk, A, C1, M, side, 0)) return 0;
    return !wl_arcchk(lk, M, C2, B, side, 0);
}
/* the lead-in side (FUN_18019c7c0 hk l.8023ff, the path of dis 18019c853-18019d5cd traced on op 4): 1 = clear, 0 = blocked (the retries of dis
   18019d0b8-18019d4a4 follow), -1 = NOT DECODED */
static int wl_chk_in(WlLead *L, const double *S, double zS, double top, int n, const double *C, int side, const double *H) {
    C3Link *lk = L->lk;
    if (n < 1) {                                                               /* dis 18019cad5-18019cb4a: within a half turn -> the one-arc path (NOT traced) */
        double cr = (S[0] - C[0]) * (H[1] - C[1]) + (H[0] - C[0]) * -(S[1] - C[1]);
        if ((cr > 0.0) == (side != 0)) return -1;
    }
    double dz = (top - zS) / (double)(n + 1), M[3];                            /* dis 18019cc2a-18019cd4a */
    if (n > 0 || wl_sweep(C, H, S, side) == 0.0) { M[0] = C[0] * 2.0 - S[0]; M[1] = C[1] * 2.0 - S[1]; }
    else wl_arcpt(C, H, S, side, 0.5, M);
    double base = zS < top ? zS : top;                                         /* (min with *(emitter[0] + 0x450): see wl_dropz) */
    if (wl_dropz(lk, M[0], M[1], base) > zS + dz * 0.5) return 0;              /* dis 18019ce2a -> 18019d0b8 */
    if (wl_dropz(lk, H[0], H[1], base) > zS + dz) return 0;                    /* dis 18019cee5 -> 18019d0b8 */
    const double *A0 = n > 0 ? S : H;
    double A[3] = { A0[0], A0[1], zS + dz }, C1[3] = { C[0], C[1], zS + dz * 0.75 }, Mm[3] = { M[0], M[1], zS + dz * 0.5 },
           C2[3] = { C[0], C[1], zS + dz * 0.25 }, B[3] = { S[0], S[1], zS };
    if (wl_arcchk(lk, A, C1, Mm, side, 0)) return 0;
    return !wl_arcchk(lk, Mm, C2, B, side, 0);
}
/* FUN_180176760 (decomp/hmm_linker_x17.c l.244-600; called at dis 18019f7c9): the ramp lead-in along the next pass's toolpath item, walked back
   from the pass start S (FUN_18027b030's item position; against the item's order, or along it when the frag is reversed (+0x79): either way against
   the pass's direction) with Z rising by tan(MaxRampAngle) per unit of XY length from S's Z until the lead's top: the item point that would pass the
   top is replaced by the point at that fraction of its segment (FUN_18000b4e0), at the top Z (l.552-575). A point equal to the current node (x, y
   and the running Z: FUN_1800082c0, l.437-446) is skipped. Out: the nodes from the top down to S (pts, mm; the list's order: every node is put in
   front, l.330-340 / 520-532), H = the top node, dir = the second node - H (l.578-590: a line node). The item's arcs (GridVal bit 26, l.371-415) come
   as the passes' chords here. 1 = done, <= 0 = NOT DECODED (-1 no item for an open pass, -2 the start off the item, 0 no top reached) */
static int wl_ramp(const WlPass *Pn, const double *src, int m, double top, double tana, double *H, double *dir, double **pts, int *npts) {
    const double *it = NULL; int ni = 0, k0 = 0;
    if (Pn->closed) { it = src; ni = m - 1; k0 = m - 2; }                      /* the re-split loop: src[0] = src[m - 1] = S */
    else {
        if (!Pn->item || Pn->nitem < 3) return -1;                             /* the open pass's closed item (FUN_18027b580's frag, MCAM_HMM_ITEMS) */
        double f[3], tt; int sg; wl_npt3(Pn->item, Pn->nitem, src, f, &sg, &tt);
        if (wl_d3(f, src) > 1e-6) return -2;
        it = Pn->item; ni = Pn->nitem - 1; k0 = sg;
    }
    if (ni < 2) return -3;
    int cap = 64, n = 0; double *q = malloc(sizeof(double) * 3 * cap), cur[3] = { src[0], src[1], src[2] }, z = src[2];
    memcpy(q, cur, sizeof cur); n = 1;
    for (int k = 0, i = k0; k < 4 * ni + 4; k++, i = (i - 1 + ni) % ni) {
        const double *p = it + 3 * i;
        if (p[0] == cur[0] && p[1] == cur[1] && z == p[2]) continue;           /* l.437-446 */
        double len = sqrt((p[0] - cur[0]) * (p[0] - cur[0]) + (p[1] - cur[1]) * (p[1] - cur[1])), dz = len * tana;
        z = z + dz;                                                            /* l.484 */
        if (n == cap) { cap *= 2; q = realloc(q, sizeof(double) * 3 * cap); }
        if (top < z) {                                                         /* l.552-575 */
            double t = (dz - (z - top)) / dz;
            q[3 * n] = cur[0] + t * (p[0] - cur[0]); q[3 * n + 1] = cur[1] + t * (p[1] - cur[1]); q[3 * n + 2] = top; n++;
            H[0] = q[3 * n - 3]; H[1] = q[3 * n - 2]; H[2] = top; dir[0] = cur[0] - H[0]; dir[1] = cur[1] - H[1];
            *pts = malloc(sizeof(double) * 3 * n); *npts = n;
            for (int j = 0; j < n; j++) memcpy(*pts + 3 * j, q + 3 * (n - 1 - j), sizeof(double) * 3);
            free(q);
            return 1;
        }
        q[3 * n] = p[0]; q[3 * n + 1] = p[1]; q[3 * n + 2] = z; n++;
        cur[0] = p[0]; cur[1] = p[1]; cur[2] = z;
    }
    free(q);
    return 0;
}
/* FUN_18019d5d0 (hk l.8537-8770) for a blocked lead-out (called at dis 1801a01d0 when the out check or its vertical lead stays blocked): a connect
   (FUN_1801d1e70, the retract's arguments) and FUN_1801dab60 (x2 l.1041-1160) at the previous pass's end with param_4 = (param_7 0 here ||
   frag +0x78) and param_5 = the trimming condition (LinkTrimmingStyle != 0 and LinkMaxAllowedTrimming > 0, off for a closed frag or plan kinds 2..4);
   FUN_1801daf90 -> FUN_1801d7450 is the Raster exit lead check (c3_lc_lead_r): the plain lead arc [E, C, T] when it is clear, else (attempts off)
   FUN_1801d87b0 / FUN_1801d76a0 -> the vertical lead FUN_1801d8630, E.z + R (traced on op 4: 26 / 26 calls, param_4 = param_5 = 0); then the
   LinkRampHeightOffset node above (hst_linker.md 13.3). The trim local_d8 is 0 without the attempts (no FUN_1801a47b0). Writes the lead-out into b
   (feed) and its top (offset included) into To2. 1 = done, 0 = NOT DECODED */
static int wl_d5(WlLead *L, MB *b, const WlPass *Pp, const double *E, const double *tE, double *To2, double u, double feed) {
    if (Pp->closed) return 0;                                                  /* param_4 = 1: the lead nodes skipped on a closed frag (c3_lk_s2: walked round, NOT DECODED) */
    if (L->tstyle != 0 && L->maxtrim > 0.0) return 0;                          /* param_5: the attempts (FindBetterArc / trimming): not ported for Waterline */
    double T[3] = { tE[0], tE[1], tE[2] }, nt = sqrt(T[0] * T[0] + T[1] * T[1] + T[2] * T[2]);
    if (!(nt > 0.0) || !(L->rvout > 0.0)) return 0;
    for (int i = 0; i < 3; i++) T[i] /= nt;
    C3Lead Lo; if (!c3_lk_lead(E, T, L->rvout, 0, &Lo)) return 0;
    int r = c3_lc_lead_r(L->lk, E, &Lo, 0);
    if (r == 1) { wl_varc(b, Lo.C, E, Lo.top, L->atol, u, feed); memcpy(To2, Lo.top, sizeof(double) * 3); }
    else { To2[0] = E[0]; To2[1] = E[1]; To2[2] = E[2] + c3_lc_r(Lo.C, Lo.top); mv(b, 0, To2[0] / u, To2[1] / u, To2[2] / u, feed); }   /* FUN_1801d8630 */
    To2[2] += L->off;
    mv(b, 0, To2[0] / u, To2[1] / u, To2[2] / u, feed);
    return 1;
}
/* The retract between two passes (prev = the pass P ending at E, mm; next = the pass N starting at S: src, m points) or, with no P, the first
   approach (FUN_180194140 -> FUN_18019e010 with no previous frag). FUN_18019e010 (dis 18019e010-1801a123a), its rounds (LAB_18019ed4f):
   - the out check FUN_18019bba0 (blocked: X = E, the flag local 0xf70 = bl); the in check FUN_18019c7c0 (blocked: -0x7b = sil), skipped once the
     ramp list holds nodes (dis 18019ee72-18019ee97: sil = 1);
   - sil or the previous round's in lead blocked (alp: local -0x80, dis 18019ef8d-18019ef98): the ramp lead-in FUN_180176760 (dis 18019f50f-18019f862: plan kind 0, frag +0xb9 set, plan +0x1054 /
     +0x1249 clear - traced on op 4 - and tan(MaxRampAngle) > 0), H = its top node, its direction the lead-in's (dis 18019fab7-18019fb0a);
   - the vertical leads (dis 18019f8af-18019fd84): out along the circle's tangent at X, or (bl) along the pass's end tangent (FUN_18027ad40,
     dis 18019f968-18019f9e3); in along the helix circle's tangent at H (not sil, not alp), the ramp's direction, or the pass's start tangent;
   - FUN_18019a6a0 on both (dis 18019fda6-18019fe13: dil = out blocked, al = in blocked);
   - the decision (dis 18019fe18-18019ff6d): alp and al -> done; not dil: done when not al, else another round; dil and al -> another round; dil
     alone: (the check's trim local 0x8 unchanged: always with the step 0) a rotation round, the 6th ends it (dis 18019fe58-18019fe69); X's angle
     about the lead-out centre moves by |angle(X) - angle(E)| / 6 (dis 18019ec14-18019ecca: DAT_1807bb580 = 1/6) the side's way (side 0: -),
     wrapped to [0, 2 pi) (DAT_1809bf198 = 2 pi, set at 180002300); when that passes E's angle the rotation ends (dis 1801a0048); with bl, X =
     the lead-out centre + R (cos, sin) of the new angle (dis 18019ff19-18019ff50);
   - the writers (dis 1801a00bf-1801a0fc8): bl or dil -> the lead-out fallback FUN_18019d5d0 (wl_d5); else the lead-out arc E -> X, the vertical
     arc, the offset line; al -> FUN_18019db50 (NOT PORTED: SAFE); else the lead-in top, its vertical arc and the ramp (FUN_180177080) or the helix.
   A round that would repeat unchanged without the rotation counter (not on the traced ops) keeps SAFE. 1 = the link is emitted into b (op units),
   N's start is the current point. */
static int wl_retract(WlLead *L, MB *b, const WlPass *Pp, const double *pe, const double *tE, const WlPass *Pn, const double *ps, int m, const double *tS,
                      const WlNode *N, int nn, const int *npass, double u, double feed, double plunge) {
    #define WL_SAFE(why) do { if (L->dbg) fprintf(stderr, "wl lead SAFE %s\n", why); L->nsafe++; free(rp); return 0; } while (0)
    double *rp = NULL; int nrp = 0;
    if (!L->on || (!Pn && !Pp) || (Pn && Pn->nrg < 1) || (Pp && Pp->nrg < 1)) WL_SAFE("input");
    C3Link *lk = L->lk;
    double E[2] = { 0, 0 }, zE = 0, Co[2] = { 0, 0 }, X[2] = { 0, 0 }; int so = 0;
    double S[2] = { 0, 0 }, zS = 0, Ci[2] = { 0, 0 }, H[2] = { 0, 0 }, top = 0, tana = tan(L->ain); int si = 0, n = 0;
    if (Pp) { E[0] = pe[0]; E[1] = pe[1]; zE = pe[2]; wl_side(Pp, E, tE, L->rout, Co, &so); }   /* FUN_1801a1d70 hk l.11981-12010 */
    if (Pn) {
        S[0] = ps[0]; S[1] = ps[1]; zS = ps[2];
        wl_side(Pn, S, tS, L->rin, Ci, &si);                                   /* hk l.12028-12056 */
        /* the helix top local_ed8 (dis 18019e2a5-18019e3df; frag +0xb9 set on every Waterline pass: traced) */
        double v; if (!wl_vtop(L, Pn, N, nn, npass, &v)) WL_SAFE("vtop");
        double hv = L->rvin * sin(L->ain), w = L->rvin * L->rvin - hv * hv; w = w > 0.0 ? sqrt(w) : 0.0;
        double t0 = w + v + (L->off > 0.0 ? L->off : 0.0); if (L->clr < t0) v -= t0 - L->clr;
        double cand = (double)L->rho + zS; top = cand <= v ? v : cand;
        wl_hstart(tana, top, Ci, S, zS, L->rin, si, &n, H);
    }
    if (Pp && !Pn) {                                                           /* the final exit (FUN_180197b80 -> FUN_18019e010 with no next frag,
                                                                                  dis 18019eaab-18019eb02, the path traced on op 4's exit): X = the
                                                                                  quarter turn from E about C_out the side's way */
        double dx = E[0] - Co[0], dy = E[1] - Co[1];
        X[0] = Co[0] + (so ? dy : -dy); X[1] = Co[1] + (so ? -dx : dx);
    } else if (Pp) {
        double d2 = (Ci[0] - Co[0]) * (Ci[0] - Co[0]) + (Ci[1] - Co[1]) * (Ci[1] - Co[1]), rr = L->rout + L->rin;
        if (d2 <= rr * rr && n == 0) WL_SAFE("fe1");                           /* local_fe1 with no full turn (hk l.9530): NOT DECODED */
        wl_tangpt(H, E, Co, L->rout, so, X);
    }
    double zb = zE;                                                            /* FUN_18019ab50 x tan(LiftAngle 0) = 0 (dis 18019fbb4-18019fbe5) */
    if (L->dbg) fprintf(stderr, "wl lead %s E %.9f %.9f %.9f Co %.9f %.9f so %d X %.9f %.9f | S %.9f %.9f %.9f Ci %.9f %.9f si %d top %.9f n %d H %.9f %.9f\n",
                        Pp ? (Pn ? "retract" : "exit") : "approach", E[0], E[1], zE, Co[0], Co[1], so, X[0], X[1], S[0], S[1], zS, Ci[0], Ci[1], si, top, n, H[0], H[1]);
    double angE = 0.0, angX = 0.0, step = 0.0, tp = 2.0 * M_PI;                 /* dis 18019ec0b-18019ecca */
    if (Pp && L->rout > 0.0) { angE = wl_ang(E[1] - Co[1], E[0] - Co[0]); angX = wl_ang(X[1] - Co[1], X[0] - Co[0]); step = fabs(angX - angE) * (1.0 / 6.0); }
    double Cvo[3] = { 0, 0, 0 }, To[3] = { 0, 0, 0 }, Cvi[3] = { 0, 0, 0 }, Ti[3] = { 0, 0, 0 }, Hr[3] = { 0, 0, 0 }, rd[2] = { 0, 0 };
    int bl = 0, sil = 0, ramp = 0, dil = 0, al = 0, alp = 0, cnt = 0, prevkey = -1;   /* alp: the previous round's al (local -0x80, the register bl of
                                                                                   dis 18019ef8d / 18019fa1b / 18019fe18) */
    for (;;) {
        int key;
        double Xr[2] = { X[0], X[1] };
        if (Pp) {                                                              /* FUN_18019bba0 (param_14 stays 1: it returned 1 on every traced call) */
            int r = wl_chk_out(L, E, zE, Co, so, X, zb);
            if (r < 0) WL_SAFE("out check");
            bl = !r;
            if (bl) { if (!wl_step0(L, Pp)) WL_SAFE("out retries"); Xr[0] = E[0]; Xr[1] = E[1]; }
        }
        if (!Pn) ;                                                             /* the exit: no in side (-0x7f clear) */
        else if (!ramp) {                                                      /* FUN_18019c7c0 */
            int r = wl_chk_in(L, S, zS, top, n, Ci, si, H);
            if (r < 0) WL_SAFE("in check");
            sil = !r;
            if (sil && !wl_step0(L, Pn)) WL_SAFE("in retries");
        } else sil = 1;
        if (Pn && (alp || sil)) {                                              /* FUN_180176760 (dis 18019ef8d: the previous round's al or sil) */
            free(rp); rp = NULL;
            int rr = wl_ramp(Pn, ps, m, top, tana, Hr, rd, &rp, &nrp);
            if (rr <= 0) WL_SAFE(rr == -1 ? "ramp: no item" : rr == -2 ? "ramp: start off the item" : "ramp");
            ramp = 1;
        }
        /* the vertical leads (dis 18019f8af-18019fd84) */
        if (Pp) {
            double dx, dy;
            if (L->rout > 0.0 && !bl) { dx = -(Xr[1] - Co[1]); dy = Xr[0] - Co[0]; if (so) { dx = -dx; dy = -dy; } dx *= 1.0 / L->rout; dy *= 1.0 / L->rout; }
            else { double t = sqrt(tE[0] * tE[0] + tE[1] * tE[1]); if (t == 0.0) t = 1.0; dx = tE[0] * (1.0 / t); dy = tE[1] * (1.0 / t); }
            double h = 0.0, wo = L->rvout * L->rvout - h * h; wo = wo > 0.0 ? sqrt(wo) : 0.0;
            Cvo[0] = -dx * h + Xr[0]; Cvo[1] = -dy * h + Xr[1]; Cvo[2] = wo + zb;
            To[0] = dx * L->rvout + Cvo[0]; To[1] = dy * L->rvout + Cvo[1]; To[2] = Cvo[2];
        }
        if (Pn) {
            double dx, dy, Hx = ramp ? Hr[0] : H[0], Hy = ramp ? Hr[1] : H[1];
            if (L->rin > 0.0 && !sil && !alp) { dx = -(H[1] - Ci[1]); dy = H[0] - Ci[0]; if (si) { dx = -dx; dy = -dy; } dx *= 1.0 / L->rin; dy *= 1.0 / L->rin; }
            else { double t = sqrt(rd[0] * rd[0] + rd[1] * rd[1]); if (t == 0.0) t = 1.0; dx = rd[0] * (1.0 / t); dy = rd[1] * (1.0 / t); }
            double h = sin(L->ain) * L->rvin, wi = L->rvin * L->rvin - h * h; wi = wi > 0.0 ? sqrt(wi) : 0.0;
            Cvi[0] = dx * h + Hx; Cvi[1] = dy * h + Hy; Cvi[2] = top + wi;
            Ti[0] = Cvi[0] - dx * L->rvin; Ti[1] = Cvi[1] - dy * L->rvin; Ti[2] = Cvi[2];
        }
        /* FUN_18019a6a0 on both (dis 18019fda6 / 18019fdf4); undecided keeps SAFE */
        dil = 0; al = 0;
        if (Pp && L->rvout > 0.0) { double P2[3] = { Xr[0], Xr[1], zb }; int r = c3_lk_test(lk, P2, Cvo, To); if (r < 0) WL_SAFE("a6a0out"); dil = r; }
        if (Pn && L->rvin > 0.0) { double H3[3] = { ramp ? Hr[0] : H[0], ramp ? Hr[1] : H[1], top }; int r = c3_lk_test(lk, Ti, Cvi, H3); if (r < 0) WL_SAFE("a6a0in"); al = r; }
        if (L->dbg) fprintf(stderr, "wl lead round %d bl %d sil %d ramp %d dil %d al %d X %.9f %.9f\n", cnt, bl, sil, ramp, dil, al, Xr[0], Xr[1]);
        if (alp && al) break;                                                  /* dis 18019fe18-18019fe1e */
        if (!dil && !al) break;
        if (!dil || al) {                                                      /* another round without the counter */
            key = bl * 8 + sil * 4 + ramp * 2 + al;
            if (key == prevkey) WL_SAFE("round repeats");                     /* the DLL would run the same round again */
            prevkey = key; alp = al; continue;
        }
        if (++cnt >= 6) break;                                                 /* dis 18019fe58-18019fe69 */
        int was = angE > angX;
        angX += so ? step : -step;
        if (angX < 0.0) angX += tp; else if (tp < angX) angX -= tp;
        if ((angE > angX) != was) break;                                       /* dis 1801a0048: past E (p23 written there is replaced by the fallback) */
        if (bl) { X[0] = cos(angX) * L->rout + Co[0]; X[1] = sin(angX) * L->rout + Co[1]; }
        prevkey = -1; alp = al;
    }
    if (bl) { X[0] = E[0]; X[1] = E[1]; }
    if (al) WL_SAFE("in lead blocked: FUN_18019db50");
    /* the lead-in side's top (dis 1801a0960-1801a0c64): the drop at it (connect base = its Z), FUN_1801dd980 (no 0x13cc frag: unchanged), + the offset */
    double Ti2[3] = { Ti[0], Ti[1], Ti[2] };
    if (Pn) {
        Ti2[2] = wl_dropz(lk, Ti[0], Ti[1], Ti[2]);
        if (Ti2[2] < Ti[2]) Ti2[2] = Ti[2];
        Ti2[2] += L->off;
        if (!(Ti2[2] > Ti[2])) WL_SAFE("topin");
    }                                    /* dis 1801a0c54 (no offset): NOT traced */
    double To2[3] = { To[0], To[1], To[2] + L->off };
    if (Pp && !(bl || dil) && wl_dropz(lk, To2[0], To2[1], To2[2]) > To2[2]) WL_SAFE("topout");   /* a higher drop: dis 1801a04c8, NOT DECODED */
    /* emit: the exit (dis 1801a00bf-1801a0582) at the feed: the fallback FUN_18019d5d0, else the lead-out arc FUN_18019b590, the point X, the vertical
       arc FUN_18011a840 and the offset line FUN_1801194d0 */
    int from = b->n;
    if (Pp && (bl || dil)) { if (!wl_d5(L, b, Pp, pe, tE, To2, u, feed)) { b->n = from; WL_SAFE("d5d0"); } }
    else if (Pp) {
        if (!(E[0] == X[0] && E[1] == X[1])) wl_arcmv(b, Co, E, zE, wl_sweep(Co, E, X, so), so, X, zb, sqrt((E[0] - Co[0]) * (E[0] - Co[0]) + (E[1] - Co[1]) * (E[1] - Co[1])), L->atol, u, feed);
        double X3[3] = { X[0], X[1], zb };
        if (L->rvout > 0.0) wl_varc(b, Cvo, X3, To, L->atol, u, feed);
        mv(b, 0, To2[0] / u, To2[1] / u, To2[2] / u, feed);
    }
    /* the height between the two tops (FUN_1801990c0, LinkRetractStyle 2 = c3_lk_pathz) */
    double h = L->clr;
    if (Pp && !Pn) {                                                           /* the exit: up to LinkZClearance (FUN_180197b80, as c3_link_end) */
        mv(b, 1, To2[0] / u, To2[1] / u, h / u, 0);
        L->nexit++; if (bl || dil) L->nfall++;
        free(rp); return 1;
    }
    if (Pp) { h = c3_lk_pathz(lk, To2, Ti2); mv(b, 1, To2[0] / u, To2[1] / u, h / u, 0); }
    /* the retract / approach: across at h, down to the lead-in top + offset; the lead-in writer (the plunge line, the vertical arc FUN_18011a840,
       then the ramp FUN_180177080 or the helix FUN_18011a350 / FUN_18019b590) at the plunge rate (hst_linker.md 13.3) */
    mv(b, 1, Ti2[0] / u, Ti2[1] / u, h / u, 0);
    mv(b, 1, Ti2[0] / u, Ti2[1] / u, Ti2[2] / u, 0);
    mv(b, 0, Ti[0] / u, Ti[1] / u, Ti[2] / u, plunge);
    if (ramp) {
        double H3[3] = { Hr[0], Hr[1], top };
        if (L->rvin > 0.0) wl_varc(b, Cvi, Ti, H3, L->atol, u, plunge);
        for (int k = 1; k < nrp; k++) mv(b, 0, rp[3 * k] / u, rp[3 * k + 1] / u, rp[3 * k + 2] / u, plunge);
    } else {
        double H3[3] = { H[0], H[1], top };
        if (L->rvin > 0.0) wl_varc(b, Cvi, Ti, H3, L->atol, u, plunge);
        if (!(H[0] == S[0] && H[1] == S[1])) {
            double a0 = wl_ang(H[1] - Ci[1], H[0] - Ci[0]), a1 = wl_ang(S[1] - Ci[1], S[0] - Ci[0]), sw;
            if (!si) { if (a1 < a0) a0 -= tp; sw = a1 - a0; } else { if (a0 < a1) a0 += tp; sw = a0 - a1; }
            if (n > 0) {                                                       /* FUN_18011a350: k more full turns (pitch (top - zS) / n) */
                int k = (int)((top - zS) / ((top - zS) / (double)n) - sw / tp + 1.0); if (k < 1) k = 0;
                sw += (double)k * tp;
            }
            wl_arcmv(b, Ci, H, top, sw, si, S, zS, L->rin, L->atol, u, plunge);
        }
    }
    free(rp);
    if (Pp) L->nret++; else L->nappr++;
    if (Pp && (bl || dil)) L->nfall++;
    if (ramp) L->nramp++;
    return 1;
    #undef WL_SAFE
}
static void wl_link(WlPass *W, int nw, const WlNode *N, int nn, int fail, MB *b, double fp, double plunge, double feed, int *down, double u, double dov,
                    double maxstay, double safez, int tr, double ramp, int *nloops, double cx, double cy, C3StayDown *sdw, WlLead *wv) {
    C3Emit em = { b, fp, plunge, feed, down, NULL, NULL, 0 };
    double *pbuf = NULL; int pn = 0, pcl = 0;                                   /* the previous pass as emitted (mm) */
    double *buf = NULL; int capb = 0;
    if (fail || !nw) {
        for (int i = 0; i < nw; i++) {
            WlPass *P = &W[i]; int m = P->n, s0 = 0;
            if (P->closed) { double bd = 1e300; for (int q = 0; q + 1 < m; q++) { double d = hypot(P->p[3 * q] / u - cx, P->p[3 * q + 1] / u - cy); if (d < bd) { bd = d; s0 = q; } } }
            double *o = malloc(sizeof(double) * 3 * m);
            for (int q = 0; q < m; q++) { int v = P->closed ? (s0 + q) % (m - 1) : q; for (int t = 0; t < 3; t++) o[3 * q + t] = P->p[3 * v + t] / u; }
            c3_emit_run(o, m, &em); free(o); (*nloops)++;
            V3 e = lastp(b); cx = e.x; cy = e.y;
        }
        return;
    }
    /* the child regions holding passes (FUN_180184200, through regions without passes), the lists +0x110 / +0x340 */
    int *npass = calloc(nn ? nn : 1, sizeof(int)); for (int i = 0; i < nw; i++) if (W[i].node >= 0) npass[W[i].node]++;
    char *child = calloc((size_t)(nn ? nn : 1) * (nn ? nn : 1), 1);
    for (int a = 0; a < nn; a++) for (int c = 0; c < nn; c++) if (npass[c]) child[(size_t)a * nn + c] = (char)wl_child_has(N, nn, npass, a, c);
    for (int fi = 0; fi < nw; fi++) for (int gi = 0; gi < nw; gi++) {
        if (W[fi].node < 0 || W[gi].node < 0 || !child[(size_t)W[fi].node * nn + W[gi].node]) continue;
        RPass A = { W[fi].p, W[fi].n, 0, NULL, 0 }, B = { W[gi].p, W[gi].n, 0, NULL, 0 };
        if (!rp_overlap(&A, &B, dov)) continue;
        W[fi].succ = realloc(W[fi].succ, sizeof(int) * (W[fi].nsucc + 1)); W[fi].succ[W[fi].nsucc++] = gi;
        W[gi].pre = realloc(W[gi].pre, sizeof(int) * (W[gi].npre + 1)); W[gi].pre[W[gi].npre++] = fi;
    }
    if (getenv("MCAM_WL_ORDER")) for (int i = 0; i < nw; i++) fprintf(stderr, "wl pass %d node %d z %.6f n %d closed %d npre %d nsucc %d start %.4f %.4f end %.4f %.4f\n", i, W[i].node, W[i].p[2] / u, W[i].n, W[i].closed, W[i].npre, W[i].nsucc, W[i].p[0] / u, W[i].p[1] / u, W[i].p[3 * W[i].n - 3] / u, W[i].p[3 * W[i].n - 2] / u);   /* testing */
    char *used = calloc(nw, 1); int *cand = malloc(sizeof(int) * nw), prev = -1; double pend[3] = { 0, 0, 0 };
    double wz0 = 99999.9921875;                                                /* the emitter's box (FUN_1801929d0 hk l.1615-1620: min 0x40f869ffe0000000 / max -1e29 per axis) */
    for (int i = 0; i < nw; i++) for (int q = 0; q < W[i].n; q++) if (W[i].p[3 * q + 2] < wz0) wz0 = W[i].p[3 * q + 2];   /* its +0x38 = the lowest Z of the passes (WZ0) */
    double st[3] = { -9999.0 * u, -9999.0 * u, safez };                        /* LinkStartX / Y (mcmsurf FUN_1800199d0: -9999 op units), LinkSafeZ */
    for (int it = 0; it < nw; it++) {
        int c = -1, nc = 0;
        if (prev >= 0) {                                                         /* (1) FUN_1801c2d70: the previous pass's list */
            for (int t = 0; t < W[prev].nsucc; t++) if (!used[W[prev].succ[t]]) cand[nc++] = W[prev].succ[t];
            if (nc) c = wl_best(W, cand, nc, pend);
        }
        if (c < 0) {                                                             /* (2)-(4): the free passes, else every pass not taken */
            nc = 0; for (int g = 0; g < nw; g++) if (!used[g] && !wl_blocked(W, used, g)) cand[nc++] = g;
            if (!nc) for (int g = 0; g < nw; g++) if (!used[g]) cand[nc++] = g;
            if (prev >= 0) c = wl_best(W, cand, nc, pend);
            else {                                                               /* the first pass: FUN_1801b9820 without a previous pass */
                RpSc *sc = malloc(sizeof(RpSc) * nc); for (int t = 0; t < nc; t++) { sc[t].d = wl_score(&W[cand[t]], st); sc[t].j = cand[t]; }
                qsort(sc, nc, sizeof *sc, rp_sc_cmp);
                int Nn = sc[0].j; c = Nn;
                for (int t = 1; t < nc; t++) {
                    const WlPass *C = &W[sc[t].j]; const double *ce = C->p + 3 * (C->n - 1);
                    if (C->closed || hypot(W[Nn].p[0] - ce[0], W[Nn].p[1] - ce[1]) > maxstay) break;
                    int nb = 0; for (int q = 0; q < W[Nn].nsucc; q++) if (W[Nn].succ[q] == sc[t].j) nb = 1;
                    for (int q = 0; q < C->nsucc; q++) if (C->succ[q] == Nn) nb = 1;
                    if (!nb) break;
                    c = sc[t].j;
                }
                free(sc);
            }
        }
        if (wl_blocked(W, used, c)) {                                            /* FUN_1801b9490: the nearest free prerequisite down the chains */
            int *fa = malloc(sizeof(int) * nw), nf = 0; wl_free_anc(W, used, c, fa, &nf, 0);
            if (nf) c = wl_best(W, fa, nf, prev >= 0 ? pend : st);
            free(fa);
        }
        const WlPass *P = &W[c]; int m = P->n; const double *src = P->p; double rdist = 0; int stay = 0;
        if (capb < 3 * (m + 2)) { capb = 3 * (m + 2); buf = realloc(buf, sizeof(double) * capb); }
        if (P->closed) {                                                         /* FUN_1801c05c0: re-split at the point nearest the previous end, trimmed by the ramp */
            double S[3]; int sg; double tt; const double *ref = prev >= 0 ? pend : st; wl_npt3(P->p, P->n, ref, S, &sg, &tt);
            double dist = 0;
            if (prev >= 0 && hypot(S[0] - pend[0], S[1] - pend[1]) < maxstay && W[prev].node >= 0 && child[(size_t)W[prev].node * nn + P->node]) stay = 1;
            if (stay && tr != 2)
            {   /* FUN_1801c3dd0 (x1 l.2063-2070): d = |dZ| / sqrt((1 - c^2) / c^2), c = cos(SurfaceRampAngle) (+0x1210) */
                double c = cos(ramp * M_PI / 180.0), c2 = c * c;
                dist = fabs(pend[2] - S[2]) / sqrt((1.0 - c2) / c2);
            }
            m = wl_resplit(P->p, P->n, ref, dist, buf); src = buf; rdist = dist;
        }
        else if (prev >= 0 && hypot(src[0] - pend[0], src[1] - pend[1]) < maxstay && W[prev].node >= 0 && P->node >= 0 && child[(size_t)W[prev].node * nn + P->node])
            stay = 1;                                                            /* an open next pass: the same stay-down test (FUN_1801c05c0 hk l.33551-33608) */
        double pe0[3]; memcpy(pe0, pend, sizeof pe0);
        memcpy(pend, src + 3 * (m - 1), sizeof pend);
        double *o = malloc(sizeof(double) * 3 * m); for (int q = 0; q < 3 * m; q++) o[q] = src[q] / u;
        int linked = 0;
        if (sdw && P->closed && pcl && rdist > 0 && *down)                      /* the surface ramp between closed passes (wl_ramp_link) */
            linked = wl_ramp_link(sdw, b, pbuf, pn, src, m, rdist, tr == 0, u, feed, wz0, 1);
        else if (sdw && P->closed && !pcl && prev >= 0 && W[prev].item && rdist > 0 && *down) {
            /* an open previous pass (FUN_1801c3dd0 x1 l.2148-2215, plan kind 0, frag +0x78 0): when its toolpath item is closed (the item record's
               +0x20, FUN_180015d30) FUN_18027b580 (decomp/hmm_linker_x15.c l.1-70) makes the frag of the whole item, re-split at the pass's end
               (FUN_180281580), and that closed frag takes P's place: the ramp's first piece is d along the item past the pass's end */
            double *ext = malloc(sizeof(double) * 3 * (W[prev].nitem + 2));
            int ne = wl_resplit(W[prev].item, W[prev].nitem, pbuf + 3 * (pn - 1), 0.0, ext);
            memcpy(ext, pbuf + 3 * (pn - 1), sizeof(double) * 3); memcpy(ext + 3 * (ne - 1), pbuf + 3 * (pn - 1), sizeof(double) * 3);
            linked = wl_ramp_link(sdw, b, ext, ne, src, m, rdist, tr == 0, u, feed, wz0, 0);   /* the link's own frag P is open: +0xa0 = 0, no
                                                                                   wander pass (traced: op 4's first ramp keeps its nodes below that hull) */
            free(ext);
        }
        else if (sdw && rdist > 0) sdw->nsafe++;
        if (!linked && !stay && wv && m >= 2 && (prev < 0 || pn >= 2)) {
            /* a retract (kind 2: not a stay-down link) or the first approach with the horizontal leads (wl_retract; the DLL's FUN_1801a1d70 /
               FUN_180194140): the previous pass's end and unit tangent (FUN_18027ad40 at the frag's end: its last segment here), the next one's start */
            double tE[3] = { 0, 0, 0 }, tS[3];
            wl_tan(src, 3, m, 0, tS);
            if (prev >= 0) { const double *e = pbuf + 3 * (pn - 1); wl_tan(e, -3, pn, 1, tE); }
            if (wl_retract(wv, b, prev >= 0 ? &W[prev] : NULL, pe0, tE, P, src, m, tS, N, nn, npass, u, feed, plunge)) { linked = 1; *down = 1; }
        }
        if (linked) { for (int q = 1; q < m; q++) mv(b, 0, o[3 * q], o[3 * q + 1], o[3 * q + 2], feed); }
        else c3_emit_run(o, m, &em);
        free(o); (*nloops)++;
        pbuf = realloc(pbuf, sizeof(double) * 3 * m); memcpy(pbuf, src, sizeof(double) * 3 * m); pn = m; pcl = P->closed;
        used[c] = 1; prev = c;
        if (getenv("MCAM_WL_ORDER")) fprintf(stderr, "wl order %d: pass %d node %d z %.6f start %.6f %.6f\n", it, c, P->node, src[2] / u, src[0] / u, src[1] / u);   /* testing */
    }
    if (wv && prev >= 0 && *down && pn >= 2) {                                /* the final exit (FUN_180197b80 -> FUN_18019e010, no next frag) */
        double tE[3]; wl_tan(pbuf + 3 * (pn - 1), -3, pn, 1, tE);
        wl_retract(wv, b, &W[prev], pend, tE, NULL, NULL, 0, NULL, N, nn, npass, u, feed, plunge);
    }
    free(used); free(cand); free(npass); free(child); free(buf); free(pbuf);
    if (getenv("MCAM_3D_DEBUG")) fprintf(stderr, "waterline linker order: %d passes, %d regions; surface ramps %d (SAFE %d); retracts %d, first approach %d (SAFE %d; lead-out fallbacks %d, ramp lead-ins %d, exit %d; trimming style %d)\n", nw, nn,
                                         sdw ? sdw->nstay : 0, sdw ? sdw->nsafe : 0, wv ? wv->nret : 0, wv ? wv->nappr : 0, wv ? wv->nsafe : 0, wv ? wv->nfall : 0, wv ? wv->nramp : 0, wv ? wv->nexit : 0, wv ? wv->tstyle : -1);
}
/* wl_link with the linker's connect (gen_waterline's call) */
static void wl_link_hst(WlPass *W, int nw, const WlNode *N, int nn, int fail, MB *b, double fp, double plunge, double feed, int *down, double u, const C3Mesh *M,
                        double R, double Rc, double sxy, double sz, double tol, double D, double wl_maxstay, double wl_safez, int wl_tr, double wl_ramp,
                        int *nloops, double cx, double cy, WlLead *wv) {
    /* the linker's connect (the surface ramps, wl_ramp_link): the step block FUN_180252d90 and the linker's tool profile with its layer-0 drop, as for
       Raster (gen_raster: FUN_1802f5270, FUN_180305960's defaults) - measured with mchmm.dll on TUTORIAL 2 SWARF op 4 (HMM_TRACE 180252d90:R inside
       FUN_180199220: 0.003 / 0.0127 / 0.879515 / 2 mm, profile 7.616952 / 4.441952 / 3.175 = these formulas with stock 1.27 mm); SurfaceRampMaxDeviation
       0x1a80 = 0.0005 mm and SurfaceRampMaxVerticalChange 0x1cfb = 1e6 (the Waterline Linking Plan's defaults FUN_1803bd270, hl l.18940-18975; mckernel's
       Waterline macro writes neither, mk l.89903-90011) */
    HstDrop wdpl; int wdpl_on = 0;
    double lk_S = R + sxy - sz, lk_t = sz - (0.02 / 100.0) * 2.0 * (sz + lk_S), lk_c = lk_t < 0 ? fmax(Rc, -lk_t) : Rc;
    RasterDrop wrdl = { &wdpl, sz }; RasterDropMm wrdm = { &wrdl, u, NULL, 0.0, 0 };
    C3Link wlk; memset(&wlk, 0, sizeof wlk);
    wlk.zdrop = raster_zdrop_mm; wlk.zctx = &wrdm; wlk.mm = 1.0; wlk.hmin = 0.003; wlk.smt = tol * u; wlk.maxstay = wl_maxstay;
    { double tsr = (R + sxy) * u, st = tol * u; wlk.maxh = tsr > st ? 2.0 * sqrt((2.0 * tsr - st) * st) : 2.0 * tsr; wlk.maxv = 2.0;
      wlk.tr0 = (lk_S + lk_t) * u; wlk.trc = (lk_c + lk_t) * u; wlk.tfl = (lk_S - lk_c) * u; }
    C3StayDown wsd; memset(&wsd, 0, sizeof wsd);
    wsd.maxstay = wl_maxstay; wsd.maxdev = 0.0005; wsd.maxvert = 1e6; wsd.cn = &wlk; wsd.key = -1; wsd.dia = D * u;
    C3StayDown *sdw = NULL;
    if (!fail && wl_tr != 2 && !getenv("MCAM_WL_NOLINKS")) { hd_build(&wdpl, M, lk_S - lk_c, lk_c + lk_t); wdpl_on = 1; sdw = &wsd; }   /* MCAM_WL_NOLINKS (testing): the SAFE rule everywhere, as before */   /* Transitions Straight (SurfaceRampStyle 0): kind 1, NOT DECODED here */
    if (getenv("MCAM_WL_DROPPROBE") && wdpl_on) {                              /* testing: the linker's drop at given XY (mm), "x y" per line */
        FILE *pf = fopen(getenv("MCAM_WL_DROPPROBE"), "r"); double x, y;
        while (pf && fscanf(pf, "%lf %lf", &x, &y) == 2) {
            fprintf(stderr, "wl drop %.17g %.17g -> %.17g", x, y, raster_zdrop_mm(&wrdm, x, y));
            const char *pr = getenv("MCAM_WL_PROF"); double fo, rb;          /* "Fo,Rb;Fo,Rb..." in op units: other profiles at the same point */
            while (pr && sscanf(pr, "%lf,%lf", &fo, &rb) == 2) {
                HstDrop d2; hd_build(&d2, M, fo, rb); RasterDrop r2 = { &d2, sz }; RasterDropMm m2 = { &r2, u, NULL, 0.0, 0 };
                fprintf(stderr, " | %g,%g -> %.9g", fo, rb, raster_zdrop_mm(&m2, x, y)); hd_free(&d2);
                pr = strchr(pr, ';'); if (pr) pr++;
            }
            fputc('\n', stderr);
        }
        if (pf) fclose(pf);
    }
    WlLead *wvl = NULL;
    if (sdw && wv && wv->on) { wv->lk = &wlk; wlk.clr = wv->clr; wvl = wv; }   /* the check connects use the same step block and drop (FUN_1801d1c20, layers 0) */
    wl_link(W, nw, N, nn, fail, b, fp, plunge, feed, down, u, 2.2 * (R + sxy - sz) * u, wl_maxstay, wl_safez, wl_tr, wl_ramp, nloops, cx, cy, sdw, wvl);
    if (wdpl_on) hd_free(&wdpl);
    if (getenv("MCAM_SD_DEBUG") && sdw) fprintf(stderr, "waterline stay-down check: FUN_1801cf230 %ld FUN_1801ce270 %ld FUN_1801cf7b0 %ld\n", wsd.ncf230, wsd.nce270, wsd.ncf7b0);
}
/* the end refinement's bisection (FUN_180361bd0, see gen_raster): from the kept line parameter ain toward the removed aout; the kept midpoints
   (nearest to ain first) in *out, their count returned */
typedef struct { const HstDrop *dp; double s_up, lo, hi, hmin, u1x, u1y, u2x, u2y, d; } RasterBis;
static int raster_bisect(const RasterBis *s, double ain, double aout, double **out) {
    int n = 0, cap = 0; *out = NULL;
    for (int it = 0; it < 60 && fabs(aout - ain) > s->hmin; it++) {
        double a = 0.5 * (ain + aout), x = a * s->u1x + s->d * s->u2x, y = a * s->u1y + s->d * s->u2y, z = hd_tip(s->dp, x, y);
        if (z > -1e29) z += s->s_up;
        if (z >= s->lo && z <= s->hi) {
            if (n == cap) { cap = cap ? 2 * cap : 8; *out = realloc(*out, sizeof(double) * 3 * cap); }
            (*out)[3 * n] = x; (*out)[3 * n + 1] = y; (*out)[3 * n + 2] = z; n++; ain = a;
        } else aout = a;
    }
    return n;
}
static int gen_raster(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "Raster here needs the Top tool plane at the origin"); return 0; }
    uint32_t rn = 0; uint8_t *rec = hst_record(f, p, NULL, &rn);
    if (!rec || rn < 0x1760) { free(rec); snprintf(op->error, sizeof op->error, "no Raster record (Mastercam's default op 132 style 5 is missing)"); return 0; }
    #define RD(o) c3d(rec, rn, o)
    #define RB(o) c3b(rec, rn, o)
    const char *why = NULL; double alo = RD(0x1248), ahi = RD(0x1250);
    if (RB(0x16d4)) why = "Down Mill / Up Mill (LinkDirectionStyle 2 / 3): the linker is NOT DECODED";
    else if (RB(0x16cc)) why = "Perpendicular fill (the second raster's stepover cutoff reader is NOT DETERMINED)";
    else if (RB(0x16bf)) why = "the approximate start point is not ported";
    else if (fabs(fmin(alo, ahi)) > 1e-6 || fabs(fmax(alo, ahi) - M_PI / 2) > 1e-6) why = "contact angle limits other than 0 to 90 degrees: the filter is NOT DETERMINED";
    else if (RB(0x1730)) why = HST_CONTACT_WHY;
    else if (!RB(0x16cf)) why = HST_NOFLUTE_WHY;
    if (why) { snprintf(op->error, sizeof op->error, "Raster: %s", why); free(rec); return 0; }
    int metric = mcam_is_metric(f);
    double D = p->tool_dia > 0 ? p->tool_dia : RD(0x350), R = D * 0.5;
    double Rc = fmin(fmax(p->tool_dia > 0 ? p->corner_r : RD(0x358), 0.0), R);
    double step = RD(0x1230); if (!(step > 1e-12)) step = metric ? 2.5 : 0.1;       /* MachStepover TP+0x1b8 (mk l.87353-87378) */
    double A = RD(0x12f0), sxy = fmax(RD(0x13c0), RD(0x13c8)), sz = RD(0x13c8), ztol = RD(0x12a0), tol = c3_step_tol(rec, rn, metric);          /* StepMachTolerance (c3_step_tol) */
    int zig = RB(0x16bd) != 0, other = RB(0x16d5) != 0;
    /* the stay-down links (C3StayDown above): SurfaceRampStyle = (0x16d8 != 1) (mk l.89903-89918); LinkMaxStayOnSurface (mk l.89205-89235): radio 0x16cd
       (K+0x468) 1 = tool diameter x 0x1310 (K+0x288) / 100, 0 = 0x1308 (K+0x280), else 2.5 mm / 0.15 in */
    int ramp1 = RB(0x16d8) != 1, gapmode = RB(0x16cd);
    double maxstay = gapmode == 1 ? D * RD(0x1310) / 100.0 : gapmode == 0 ? RD(0x1308) : (metric ? 2.5 : 0.15);
    /* Use Z depths (0x16c9): PosZHi / PosZLo = max / min of TP+0x1c0 / 0x1c8 (record 0x1238 / 0x1240) + job +0x28 (mk l.87321-87344) */
    int zdep = RB(0x16c9); double zd1 = RD(0x1238), zd2 = RD(0x1240), sxy0 = RD(0x13c0);
    /* the retract links (C3Link): LinkRetractStyle 2 (record 0x16c4, mk l.89274-89300), LinkZClearance = the clearance 0x170 when on (0x179) and absolute
       (0x178 = 0) (mk l.89177-89205), vertical lead radii 0x1348 / 0x1350, LinkRampHeightOffset = the feed plane 0x190 (mk l.89988-89992) */
    int lk_on = RB(0x16c4) == 2 && RB(0x179) && !RB(0x178) && RD(0x12a0) > 0;
    /* LinkSafeZ / LinkZClearance (FUN_180075b48, its l.46-67 from mk l.88917; tools/hmm_macro.py link_params): the clearance K+0x10 (record 0x170) + job +0x28, minus the corner radius when K+0x48
       (record 0x2f8) is 0; written when the clearance is on and absolute and (K+0x469 (record 0x16ce) == 0 or LinkRetractStyle 2), as lk_on requires - else the plans'
       default (RoundUp, NOT DECODED) */
    int safez_on = RB(0x179) && !RB(0x178) && (RB(0x16ce) == 0 || RB(0x16c4) == 2);
    double safez = RD(0x170) - (RB(0x2f8) == 0 ? Rc : 0.0);
    double lk_clr = safez, lk_rin = RD(0x1348), lk_rout = RD(0x1350), lk_off = RD(0x190);
    /* LinkTrimmingStyle 0x1b1c = 0 when min(lead in, lead out) <= the SystemContext +0x10 tolerance (NOT DECODED; taken as 0 as tools/hmm_macro.py
       does), else K+0x46c (record 0x16d1); LinkMaxAllowedTrimming = K+0x308 (record 0x13a0) (mk l.89237-89272); LinkTrimMinOverlapDiff is not written
       by mckernel: its default LinkMaxAllowedTrimming / 100 (hl l.19195-19260), connect +0x200 = (float) of it (FUN_1801d1e70 arg 10) */
    int lk_tstyle = fmin(lk_rin, lk_rout) <= 0.0 ? 0 : RB(0x16d1); double lk_maxtrim = RD(0x13a0);
    int flute_contact = RB(0x16cf) != 0;   /* Steep / Shallow page: Flute contact (TP+0x46a; PosContactOnly = false written only when it is 0, mk l.88152) */
    /* check surfaces: Edit Passes to Surfaces 0x141f after the Raster passes (cam3d_checktrim.c) */
    char cerr[200]; C3Chk *chk = c3_chk_open(f, op, rec, rn, ztol > 1e-9 ? ztol : tol * 0.1, R, Rc, metric, cerr, sizeof cerr);
    if (!chk && cerr[0]) { snprintf(op->error, sizeof op->error, "Raster: %s", cerr); free(rec); return 0; }
    free(rec);
    #undef RD
    #undef RB
    if (!(D > 0)) { snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); c3_chk_close(chk); return 0; }
    C3Mesh M; char err[150];
    if (!c3_mesh_op(f, op, ztol > 1e-9 ? ztol : tol * 0.1, &M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); c3_chk_close(chk); return 0; }
    if (getenv("MCAM_MESH_STL") && !c3_mesh_stl(getenv("MCAM_MESH_STL"), metric ? 1.0 : 25.4, &M)) { snprintf(op->error, sizeof op->error, "MCAM_MESH_STL: cannot read %s", getenv("MCAM_MESH_STL")); c3_mesh_free(&M); c3_chk_close(chk); return 0; }
    /* PosZHi / PosZLo (hst3d_port_plan.md 6.7, style 4 in the mask 0x5b6: PosZLo lowered by the corner radius; Use Z depths replaces them, mk l.87321-87344, l.87940-87950). Applied by the Z window FUN_180361bd0 / FUN_1802f3a80 (hst_z_filter): positions outside are removed. EnableTrimToZRange (FUN_1802ed680) is false by default and not set by the Raster macro */
    double pzhi = M.hi[2] + (sz > 0 ? sz : 0) + ztol, pzlo = M.lo[2] + (sz < 0 ? sz : 0) - ztol - Rc;
    if (zdep) { pzhi = fmax(zd1, zd2); pzlo = fmin(zd1, zd2); }
    /* the tool as the slice's (one ball Rb with flat radius Fo, the stocks) */
    int flat_tip = Rc + sz <= 0.0;
    double Rb = flat_tip ? 0.0 : Rc + sz, Fo = flat_tip ? R + sxy : (R - Rc) + sxy - sz, s_up = sz;   /* the tip: the floor stock above the inflated tool's bottom */
    double hcell = fmin(fmax(fmax(ztol, tol) * 4, R / 16), R / 4);
    HstDrop dp; hd_build(&dp, &M, Fo, Rb);   /* CToolDropPos: the exact drop on the triangles (replaces the height field, A10) */
    SrHf H; memset(&H, 0, sizeof H); (void)flute_contact;   /* Flute contact: cells off the model
                                                                  are no surface (the classic ops' fill to the lowest Z is theirs, sr 2.3) */
    /* the containment (the op's chains as tmptc.tmp holds them, c3_containment) or the drive's XY box */
    CPaths region = { 0 };
    { char ce[150]; if (!c3_containment(f, op, c3_hst_tcb_type(op), &region, ce, sizeof ce)) { snprintf(op->error, sizeof op->error, "%s", ce); free(H.z); hd_free(&dp); c3_mesh_free(&M); c3_chk_close(chk); return 0; } }
    /* no containment chain: the Bounding Box Plan 0x1425 of the surfaces with OffsetDistance = |TP+0x318| (record 0x13c0) + FUN_18006fee0 * 0.5 +
       TP+0x220 (record 0x12a0) * 2 (mk l.92848-92875, style not in the mask 0x209; FUN_18006fee0 = the tool's diameter for an untapered tool) */
    { uint32_t rn2 = 0; uint8_t *r2 = hst_record(f, p, NULL, &rn2); if (r2) { int ok = c3_tcb_offset(&region, r2, rn2, D, fmax(tol * 0.5, 1e-4)); free(r2); if (!ok) { snprintf(op->error, sizeof op->error, C3_TCB_FAIL); free(H.z); hd_free(&dp); cl_free(&region); c3_mesh_free(&M); c3_chk_close(chk); return 0; } } }   /* the containment as written to tmptc.tmp (c3_tcb_offset) */
    if (getenv("MCAM_TC")) { cl_free(&region); if (!c3_read_tc(getenv("MCAM_TC"), &region)) { snprintf(op->error, sizeof op->error, "MCAM_TC: cannot read %s", getenv("MCAM_TC")); free(H.z); hd_free(&dp); c3_mesh_free(&M); c3_chk_close(chk); return 0; } }
    if (getenv("MCAM_HMM_DUMP")) hmm_dump_tc(&region);
    if (!region.n) { double w = fabs(sxy0) + D * 0.5 + 2 * ztol; region.p = calloc(1, sizeof(CPath)); region.p[0] = c3_box_path(M.lo[0] - w, M.lo[1] - w, M.hi[0] + w, M.hi[1] + w); region.n = 1; }
    double u1x = cos(A), u1y = sin(A), u2x = sin(A), u2y = -cos(A);
    double mn1 = 1e300, mx1 = -1e300, mn2 = 1e300, mx2 = -1e300;
    #define ACC(x, y) do { double a_ = (x) * u1x + (y) * u1y, b_ = (x) * u2x + (y) * u2y; mn1 = fmin(mn1, a_); mx1 = fmax(mx1, a_); mn2 = fmin(mn2, b_); mx2 = fmax(mx2, b_); } while (0)
    /* the measured objects are the ConstraintBoundaries (FUN_18037ca90 measures the plan's +0x118 +0x438 list): the containment, else the
       Bounding Box plan's box - measured with mchmm.dll (hst_linker.md 18): RESTPASSES op 18's lines sit where the box offset by OffsetDistance
       puts them (x0 = -1.80611 in, the drive's XY range gave -1.81110, the 0.005 in shift of 17.3) */

    for (int i = 0; i < region.n; i++) for (int k = 0; k < region.p[i].n; k++) ACC(region.p[i].xy[2 * k], region.p[i].xy[2 * k + 1]);
    #undef ACC
    double e = 0, L0 = mn1 - (e + 1), L1 = mx1 + e + 1, q = (mx2 - mn2) / step; int kq = (int)q; double rr = (q - kq) * step;
    if (kq > 20000) { free(H.z); hd_free(&dp); c3_mesh_free(&M); c3_chk_close(chk); cl_free(&region); snprintf(op->error, sizeof op->error, "too many raster lines"); return 0; }
    double fp = p->top_z + p->feed_plane; if (fp < M.hi[2] + sz + p->feed_plane) fp = M.hi[2] + sz + p->feed_plane;
    double feed = p->feed > 0 ? p->feed : 100, plunge = p->plunge_feed > 0 ? p->plunge_feed : feed;
    int npass = 0, down = 0, nsamp = (int)ceil((L1 - L0) / hcell) + 1;   /* ours: the sampling step is the height-field cell; FUN_1803612d0's split rule (FUN_180252d90, defaults FUN_180305960) was tried and made the oracle worse (formats/audit_2026-11-09_highspeed_surface1.md) */
    double hfe = tol < (metric ? 0.1 : 0.003937007874015749) ? tol : (metric ? 0.1 : 0.003937007874015749);   /* the Z limit tolerance MachTrimZTolerance: the cut tolerance job +0xa0 when below 0.1 mm / 0.00394 in (mk l.88128-88134, style 4), else the plan default 0.1 (hmm_pass0) */
    double *pz = malloc(sizeof(double) * nsamp); char *in = malloc(nsamp);
    /* the stay-down links (C3StayDown): McHmm's lengths are mm (u = mm in op units); SurfaceRampTangentFactor = MachStepover (step), SurfaceRampMaxDeviation 0.005 mm,
       StepMinHorizontalChange 0.003 mm (FUN_180305960, formats/scallop.md 1), StepMachTolerance (tol). With SurfaceRampStyle 0 the link stays kind 1 (the straight
       connect with the wander FUN_1801deaf0 / 1801dde40, NOT DECODED): SAFE. */
    double u = metric ? 1.0 : 1.0 / 25.4, lkmf = metric ? 1.0 : 25.4;   /* lkmf: op units -> mm, the linker's frame (N13-27, below) */
    /* The linker's tool (EvaMachLocBasePlan +0x208 for plan kind 7, formats/hst_linker.md 11 / 21.3): FUN_1802f5270 = FUN_18013e200(ToolShaftRadius,
       LinkerToolCornerRadius) then FUN_18013e330(.., LinkerToolThickness t, 0) (decomp/hst3d_grid.c l.2957-3060: [0] = S + t, [1] = c + t, [3] = S - c);
       ToolShaftRadius S = ToolShaftRadiusUnAdjusted + (ToolThicknessXY - ToolThicknessZ) (dis 1802fbf06), LinkerToolThickness = ToolThicknessZ -
       (LinkCutterNarrowByPercent / 100) x 2 (ToolThicknessZ + S), LinkCutterNarrowByPercent 0.02 (hl l.12191-12196; mckernel does not write it),
       LinkerToolCornerRadius c = t < 0 ? max(ToolCornerRadius, -t) : ToolCornerRadius (the kind-7 defaults, dis 1802fd84e-1802fdec2). Measured with
       mchmm.dll (HMM_TRACE 180252d90:R on RESTPASSES op 18's macro, stock XY / Z 0 / 0, 0.508 / 0, 0.508 / 0.254, 0 / -0.254, 0.254 / 0.254 mm: the
       block's [0] / [1] / [3] = these to 1e-15). The connect's drop at layer 0 (FUN_1801cf500 x4 l.251-283): CToolDropPos FUN_180222920 with this
       profile from z - ToolThicknessZ (+0x1250), + ToolThicknessZ - the passes' drop with ball c + t and flat S - c instead of Rc + Z / S - Rc. */
    double lk_S = R + sxy - sz, lk_t = sz - (0.02 / 100.0) * 2.0 * (sz + lk_S), lk_c = lk_t < 0 ? fmax(Rc, -lk_t) : Rc;
    HstDrop dpl; int dpl_on = 0;                                                  /* built before the linking (below) */
    RasterDrop rdl = { &dpl, sz }; RasterDropMm rdm = { &rdl, lkmf, NULL, 0.0, 0 };
    C3Link lkl = { lk_rin * lkmf, lk_rout * lkmf, lk_off * lkmf, ztol * lkmf, lk_clr * lkmf, 1.0, 0.003, tol * lkmf, maxstay * lkmf, raster_zdrop_mm, &rdm, 0, { 0 }, { 0 }, 0, 0, 0,
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    {   /* the connect's step criteria (FUN_180252d90's block; defaults FUN_180305960, formats/scallop.md 1: MaxHorizontalStepChange = 2 sqrt((2 TSR - tol) tol)
           when TSR > tol, else 2 TSR, TSR = ThickenedShaftRadius = ToolShaftRadius + ToolThicknessZ = R + ToolThicknessXY; StepMaxVerticalChange 2.0; traced on
           ops 5 / 18: 0.003 / 0.0127 / 0.567393 / 0.261052 / 2 mm, with stock 0.508 / 0.254 mm on op 18: 0.611185) and the linker's tool profile (above) */
        double tsr = (R + sxy) * lkmf, st = tol * lkmf;
        lkl.maxh = tsr > st ? 2.0 * sqrt((2.0 * tsr - st) * st) : 2.0 * tsr; lkl.maxv = 2.0;
        lkl.tr0 = (lk_S + lk_t) * lkmf; lkl.trc = (lk_c + lk_t) * lkmf; lkl.tfl = (lk_S - lk_c) * lkmf;
    }
    lkl.tstyle = lk_tstyle; lkl.maxtrim = lk_maxtrim * lkmf; lkl.r200 = (double)(float)(lk_maxtrim * lkmf / 100.0);
    C3StayDown sdl = { maxstay * lkmf, step * lkmf, 0.005, 2.1 * tol * lkmf, &lkl, 0, -1, { 0 }, { 0 }, 0, 0, 0, 0, 0, 0, 0, D * lkmf, { 0 }, 0, 0, 0 };
    /* With check surfaces the linker's connects drop against the protect model too: FUN_1801cf7b0 -> FUN_1801cf5e0 calls FUN_1803a46d0 on every node
       (traced with mchmm.dll: 9734 calls on a synthetic Raster with a check model, 0 without; formats/hst_linker.md 21.5 / 22). The model (linker +0x7c0,
       hl l.7560-7650): the EvaBoxSet of the plan behind the passes plan's ProtectedSurfaces = the Edit Passes to Surfaces plan 0x141f's check model
       (c3_chk_open's mesh). P = LinkerProtectThickness 0x1ec9 = LinkProtectThickness(MachiningPasses, ToolThicknessZ) (builtin 1802a8120, hst_linker.md
       11.1): the passes plan is mckernel's Convert To Primary Output 0x1458 of the 0x141f plan with OutputID 0x1cb2, so OverThickness 0x1b51 of that
       plan = the check stock (c3_chk_open's t). The profile (FUN_1803a3f50 hl l.9508-9540, dis 1803a3f50-1803a4125): +0x17a0 = the linker's tool +0x208,
       then, when P - ToolThicknessZ != 0, FUN_1803a4130(+0x17a0, 0, P - ToolThicknessZ) = FUN_18013e330's offset (hst3d_grid.c l.3000-3060: corner and
       shaft + d, the flat part unchanged), not applied when corner + d < 0 (hl l.9558). */
    HstDrop dpp; int dpp_on = 0;
    C3StayDown *sdp = ramp1 ? &sdl : NULL;
    C3Link *lkp = lk_on ? &lkl : NULL;
    /* MCAM_HMM_DUMP=PREFIX (testing, tools/hmm_oracle.py): the drive mesh as PREFIX.stl and the values this port feeds the Raster passes plan
       0x1404 / Bounding Box 0x1425 / Raster Linker 0x142b as PREFIX.par ("PLAN Name value", McHmm's names, mm), for the mchmm.dll unit oracle
       (tools/peloader --hmm). The names are the plans' parameters (decomp/hmm_params.tsv); which value goes to which name is this port's reading
       of mckernel (the citations above), not a decode of the macro writer. */
    if (getenv("MCAM_HMM_DUMP")) {
        char pth[1024]; double mf = metric ? 1.0 : 25.4;
        hmm_dump(f, p, &M, metric, D, Rc, chk);
        snprintf(pth, sizeof pth, "%s.par", getenv("MCAM_HMM_DUMP")); FILE *po = fopen(pth, "w");
        if (po) {
            fprintf(po, "UNIT %.12g\nB OffsetDistance %.12g\n", mf, (fabs(sxy0) + D * 0.5 + 2 * ztol) * mf);
            fprintf(po, "R ToolShaftRadius %.12g\nR ToolCornerRadius %.12g\nR ToolFlatRadius %.12g\n", R * mf, Rc * mf, (R - Rc) * mf);
            fprintf(po, "R MachStepover %.12g\nR RasterAngle %.12g\nR StepMachTolerance %.12g\nR MachTrimZTolerance %.12g\n", step * mf, A * 180 / M_PI + 180, tol * mf, hfe * mf);
            fprintf(po, "R ToolThicknessXY %.12g\nR ToolThicknessZ %.12g\nR PosZHi %.12g\nR PosZLo %.12g\n", sxy * mf, sz * mf, pzhi * mf, pzlo * mf);
            fprintf(po, "L LinkDirectionStyle %d\nL LinkReverseDirection %s\nL LinkMaxStayOnSurface %.12g\nL SurfaceRampStyle %d\nL SurfaceRampTangentFactor %.12g\n",
                    zig ? 1 : 0, other ? "true" : "false", maxstay * mf, ramp1 ? 1 : 0, step * mf);
            if (lk_on) fprintf(po, "L LinkRetractStyle 2\nL LinkZClearance %.12g\nL LinkVerticalLeadInRadius %.12g\nL LinkVerticalLeadOutRadius %.12g\nL LinkRampHeightOffset %.12g\n",
                               lk_clr * mf, lk_rin * mf, lk_rout * mf, lk_off * mf);
            fclose(po); }
    }
    /* 1. the passes plan: every raster line's passes, each A -> B (the plan's own direction), keyed by the line (frag +0x70, 15.1 (a): one key per
       raster line, as measured with mchmm.dll, hst_linker.md 16.4 / 18); through the check trimming (Edit Passes to Surfaces 0x141f) when the op has it */
    RPassList pl = { 0 };
    double hmin = 0.003 * u;                                                     /* StepMinHorizontalChange +0xcb8 (0x1acf, default 0.003 mm; hst_linker.md 12) */
    for (int i = 0; i <= kq; i++) {
        if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); free(pz); free(in); free(H.z); hd_free(&dp); c3_mesh_free(&M); c3_chk_close(chk); cl_free(&region); rp_free(&pl); return 0; }
        double d = mn2 + i * step + rr / 2;
        for (int k = 0; k < nsamp; k++) { double a = L0 + (L1 - L0) * k / (nsamp - 1), x = a * u1x + d * u2x, y = a * u1y + d * u2y;
            in[k] = region.n ? (char)wl_inside(&region, x, y) : 1;
            double z = hd_tip(&dp, x, y); if (z > -1e29) z += s_up;   /* no contact: stays below the window, so it goes */
            pz[k] = z; }
        char *cin = malloc(nsamp); memcpy(cin, in, nsamp);                       /* the containment alone */
        { char *zk = malloc(nsamp); hst_z_filter(pz, nsamp, pzlo, pzhi, hfe, zk); for (int k = 0; k < nsamp; k++) in[k] = in[k] && zk[k]; free(zk); }
        pl.key = i;
        int k = 0;
        while (k < nsamp) {
            if (!in[k]) { k++; continue; }
            int a0 = k; while (k < nsamp && in[k]) k++;
            int a1 = k - 1;
            /* the end refinement (FUN_180361bd0's second loop, raster/grid.c l.2344-2440): between a kept position and its removed neighbour the
               position is bisected (the drop FUN_180222920 at the XY midpoint, l.2384-2407) until the XY gap is <= StepMinHorizontalChange (+0xcb8,
               l.2363-2366), the side decided by the tight Z test FUN_1802f3a80(.., 0) = [PosZLo, PosZHi] (+0x6c0 / +0x6c8, set at mchmm 1802f2e61-
               1802f2eba; l.2353-2357); the kept midpoints join the pass. Measured with mchmm.dll (hst_linker.md 18): the pass ends lie on the tight
               window - end positions that are only in the window widened by MachTrimZTolerance are not kept (0.0127 -> 0.3 mm leaves PARALLELSTEEP
               op 5's ends where they are). Only where the removed neighbour is inside the containment (the containment trimming stays at the
               samples: its rule is NOT DETERMINED). */
            while (a0 <= a1 && !(pz[a0] >= pzlo && pz[a0] <= pzhi)) a0++;
            while (a1 >= a0 && !(pz[a1] >= pzlo && pz[a1] <= pzhi)) a1--;
            if (a1 < a0) continue;
            RasterBis bs = { &dp, s_up, pzlo, pzhi, hmin, u1x, u1y, u2x, u2y, d };
            double *head = NULL, *tail = NULL; int nh = 0, nt = 0;
            if (a0 > 0 && cin[a0 - 1]) nh = raster_bisect(&bs, L0 + (L1 - L0) * a0 / (nsamp - 1), L0 + (L1 - L0) * (a0 - 1) / (nsamp - 1), &head);
            if (a1 + 1 < nsamp && cin[a1 + 1]) nt = raster_bisect(&bs, L0 + (L1 - L0) * a1 / (nsamp - 1), L0 + (L1 - L0) * (a1 + 1) / (nsamp - 1), &tail);
            int m = nh + (a1 - a0 + 1) + nt, q = 0;
            if (m < 2) { free(head); free(tail); continue; }
            double *run = malloc(sizeof(double) * 3 * m);
            for (int j = nh - 1; j >= 0; j--, q++) memcpy(run + 3 * q, head + 3 * j, sizeof(double) * 3);   /* the head's outermost point first */
            for (int j = a0; j <= a1; j++, q++) { double a = L0 + (L1 - L0) * j / (nsamp - 1); run[3 * q] = a * u1x + d * u2x; run[3 * q + 1] = a * u1y + d * u2y; run[3 * q + 2] = pz[j]; }
            for (int j = 0; j < nt; j++, q++) memcpy(run + 3 * q, tail + 3 * j, sizeof(double) * 3);
            free(head); free(tail);
            if (chk) c3_chk_trim(chk, run, m, 0, rp_push, &pl); else rp_push(run, m, &pl);
            free(run);
        }
        free(cin);
    }
    /* MCAM_HMM_PASSES=FILE (testing, tools/hmm_oracle.py --passes): the passes plan's own passes (mchmm.dll tag 1, "PASS n" + n lines "x y z", op
       units) replace the port's, so that the port's linker below runs on Mastercam's passes (link by link against the DLL's linker,
       tools/tests/link_cmp.py); the frag key = the raster line (consecutive passes with the same d = p . u2) */
    /* N13-27: the linker below runs in mm, mchmm's unit (the macro writer's x 25.4: FUN_18007d7c8 / FUN_180075b48 write every length in mm, tools/hmm_macro.py;
       mckernel divides the toolpath it reads back by 25.4 for an inch part, CMCHstExtractTag FUN_1800654c4) - the passes go into the mm frame here and
       the linked moves come back to the part's unit after the final exit (lkmf) */
    for (int j = 0; j < pl.n; j++) for (int q = 0; q < 3 * pl.v[j].n; q++) pl.v[j].p[q] *= lkmf;
    if (getenv("MCAM_HMM_PASSES")) {
        const char *hp = getenv("MCAM_HMM_PASSES"); size_t hl = strlen(hp);
        rp_free(&pl);   /* a ".mmpasses" file (hmm_oracle.py, the DLL's own mm values) is read as it is, a ".passes" file (op units) scaled */
        if (!rp_read(hp, &pl, u2x, u2y, step * lkmf, hl > 9 && !strcmp(hp + hl - 9, ".mmpasses") ? 1.0 : lkmf)) { snprintf(op->error, sizeof op->error, "MCAM_HMM_PASSES: cannot read %s", getenv("MCAM_HMM_PASSES")); free(pz); free(in); free(H.z); hd_free(&dp); c3_mesh_free(&M); c3_chk_close(chk); cl_free(&region); return 0; }
    }
    if (getenv("MCAM_HMM_DUMP")) {   /* in op units, as before */
        char pth[1024]; snprintf(pth, sizeof pth, "%s.ourpasses", getenv("MCAM_HMM_DUMP"));
        for (int j = 0; j < pl.n; j++) for (int q = 0; q < 3 * pl.v[j].n; q++) pl.v[j].p[q] /= lkmf;
        rp_write(pth, &pl);
        for (int j = 0; j < pl.n; j++) for (int q = 0; q < 3 * pl.v[j].n; q++) pl.v[j].p[q] *= lkmf;
    }
    /* 2. the linker's direction pass for plan kind 1 (FUN_1803a2750, hl l.8313-8640): groups of consecutive passes with the same frag key; group g
       (from 0) is reversed when LinkReverseDirection (Other Way, record 0x16d5) xor (LinkDirectionStyle == 1 (Zigzag 0x16bd) and g odd); a
       reversed group = every pass reversed (FUN_180281840) and the group's passes in reverse order (hl l.8573-8620) */
    for (int s = 0, g = 0; s < pl.n; g++) {
        int t = s; while (t < pl.n && pl.v[t].key == pl.v[s].key) t++;
        if (other ^ (zig && (g & 1))) {
            for (int j = s; j < t; j++) rp_reverse(&pl.v[j]);
            for (int a = s, c = t - 1; a < c; a++, c--) { RPass x = pl.v[a]; pl.v[a] = pl.v[c]; pl.v[c] = x; }
        }
        s = t;
    }
    int lk_n0 = b->n;
    /* 3. the order and 4. the links: FragOrderer with OrderingType 1 (FUN_1801b90d0, hk l.27841-27990), one cluster; the graph's edges join
       consecutive raster lines (hst_linker.md 19.1), no groups: every pass comes from FUN_1801b9820 (rp_next) over the passes the filter FUN_1801bf440 / FUN_1801be020 lets
       through (blocked while a prerequisite of the pass's +0x340 list is not taken; rp_prereq, DECODED: the overlapping passes of the previous
       line holding passes, the overlap test at 1.5 x MachStepover, hst_linker.md 19.1). FUN_1801c48d0 (hk l.36157-36190) then drops a pass whose length (FUN_180279600, [inferred] 3D) is below
       LinkRFMinPassLength (default 0.02 mm, hl l.12335-12375; mckernel writes it for kernel styles 2 / 9 only); the pass is taken all the same
       (hk l.27974-27983) and the next search starts from the last LINKED pass (+0xf08 [inferred], set by the link FUN_1801c05c0). */
    {
        double minlen = 0.02, P0[3] = { -9999.0, -9999.0, safez_on ? safez * lkmf : 0.0 };   /* LinkStartX / Y = -9999 without a start point (mcmsurf
                                                     FUN_1800199d0, hst_linker.md 17.1); LinkSafeZ (not written: its default RoundUp NOT DECODED, 0 here) */
        hd_build(&dpl, &M, lk_S - lk_c, lk_c + lk_t); dpl_on = 1;                /* the linker's drop (above) */
        if (chk) {                                                                /* the protect model (above) */
            double P = chk->t, dd = P - sz, rb = lk_c + lk_t;
            if (dd != 0.0 && rb + dd >= 0.0) rb += dd;
            hd_build(&dpp, c3_chk_mesh(chk), lk_S - lk_c, rb); dpp_on = 1; rdm.pd = &dpp; rdm.P = P;
        }
        rp_prereq(&pl, 1.5 * step * lkmf);   /* LinkORAdjacentLevelOverlap = EvaluateR(MachiningPasses, MachStepover) * 1.5 (hl l.13650-13665) */
        char *used = calloc(pl.n ? pl.n : 1, 1); int have = 0; double pe[3] = { 0 };
        lk_n0 = b->n;
        for (int t = 0; t < pl.n; t++) {
            int j = rp_next(&pl, used, have ? pe : P0, have, maxstay * lkmf);
            if (j < 0) break;
            used[j] = 1;
            RPass *P = &pl.v[j]; double len = 0;
            for (int q = 1; q < P->n; q++) len += sqrt(pow(P->p[3 * q] - P->p[3 * q - 3], 2) + pow(P->p[3 * q + 1] - P->p[3 * q - 2], 2) + pow(P->p[3 * q + 2] - P->p[3 * q - 1], 2));
            if (len < minlen) continue;
            C3Emit em = { b, fp * lkmf, plunge, feed, &down, sdp, lkp, P->key };
            c3_emit_run(P->p, P->n, &em); npass++;
            memcpy(pe, P->p + 3 * (P->n - 1), sizeof pe); have = 1;
        }
        free(used);
    }
    rp_free(&pl);
    int lk_end = npass && b->n && lkp && c3_link_end(lkp, b, feed);          /* the final exit (C3Link) while the drop is alive, else SAFE below */
    for (int k = lk_n0; k < b->n; k++) { b->m[k].p.x /= lkmf; b->m[k].p.y /= lkmf; b->m[k].p.z /= lkmf; }   /* the linker's mm frame -> the part's unit (N13-27) */
    free(pz); free(in); free(H.z); hd_free(&dp); if (dpl_on) hd_free(&dpl); if (dpp_on) hd_free(&dpp); c3_mesh_free(&M); c3_chk_close(chk); cl_free(&region); free(lkl.pp);
    if (!npass || !b->n) { snprintf(op->error, sizeof op->error, "nothing to cut"); return 0; }
    if (!lk_end) { V3 l = lastp(b); mv(b, 1, l.x, l.y, p->clearance > fp ? p->clearance : fp, 0); }   /* the final exit (C3Link) or SAFE */
    if (getenv("MCAM_3D_DEBUG")) fprintf(stderr, "raster links: stay-down %d (SAFE %d: rise %d, kind 1 split %d; rises: retried straight %d, to the retract %d, extended %d), retract / approach %d (SAFE %d, lead blocked %d, a trim %d; vertical leads %d, arc fits %d)\n", sdl.nstay, sdl.nsafe, sdl.nrise, sdl.nkind1, sdl.nretry, sdl.nret, sdl.next, lkl.nret, lkl.nsafe, lkl.nblock, lkl.ntrim, lkl.nvert, lkl.nfit);
    if (getenv("MCAM_LK_DEBUG")) fprintf(stderr, "raster lead check: FUN_1801ce270 %d FUN_1801cc840 %d FUN_1801cf7b0 %d; retract height FUN_1801ce270 %d FUN_1801cf7b0 %d; protect FUN_1803a46d0 %ld\n", lkl.ncc, lkl.ncut, lkl.ndrop, lkl.npz_ce, lkl.npz_drop, rdm.np);
    if (getenv("MCAM_SD_DEBUG")) fprintf(stderr, "raster stay-down check: FUN_1801cf230 %ld FUN_1801ce270 %ld FUN_1801cf7b0 %ld; wander %d\n", sdl.ncf230, sdl.nce270, sdl.ncf7b0, sdl.nwander);
    return 1;
}

/* ------------------------------------------------------------------------------------------------------- Spiral / Radial */
/* HST Spiral (UI style 9, kernel style 7) and Radial (UI style 10, kernel style 8): grid plans that share Raster's evaluator slot [1]
   18035ec70 (EvaSpiralPassesPlan 180790d00 / EvaRadialPassesPlan 1807910f0, ~/mastercam-re/work/hst3d/eva_vt.txt); G = decomp/raster/grid.c,
   mk = decomp/mckernel_all.c, ms = decomp/mcmsurf_all.c. Decode: formats/hst3d_port_plan.md 10.
   Parameters (FUN_180071314 styles 7 / 8, mk l.88158-88340): MachStepover = record 0x1230; RadialFocalPointX / Y = record 0x13f0 / 0x13f8 (TP+0x338 /
   0x340, + the WCS origin in the view when TP+0x50, mk l.87305-87319); RadialCloseRad = 0x1400 (TP+0x348); RadialFarRad = 0x1408 (TP+0x350), set only
   when "Outer radius" is on (record 0x16e3 = 0, the page's check 710 = (0x16e3 == 0): Spiral InitPage FUN_1800865c0 ms l.87693-87700 / OK
   FUN_180086ee0 l.88101) and, for Spiral, far - close > 10 x the tolerance 0x12a0; RadialStart / EndAngle = 0x1410 / 0x1418 radians (TP+0x358 / 0x360,
   page 1339); SpiralClockwise = 0x16df (TP+0x477, page 1338 check 678: FUN_1800a0a80 ms l.105467 / l.105512), inverted for Other Way (0x16d5) when
   neither Zigzag 0x16bd nor Down / Up Mill 0x16d4 (mk l.88166-88177); SpiralLinearisationTolerance = the cut tolerance when below 0.01 mm
   (0.000393 in), else the plan default 0.01 (CSpiralPassesPlan ctor G l.22432-22452; the default's unit [inferred] mm from the thresholds).
   Links (FUN_180075b48 mk l.89846-89875): Zigzag -> bidirectional, else one way; Other Way -> LinkReverseDirection (+ LinkReverseOrder for Spiral).
   Ours (as Raster): e = PassesExtension = 0 (the setter writes "PassExtension", the plan reads 0x1ad7 "PassesExtension"); the tool positions
   (cam3d_srough.c's height-field drop of the stock-inflated tool every cell; off the drive its lowest Z); the containment trimming; the SAFE links
   (the Spiral / Radial Linkers 0x142c / 0x142d are NOT DECODED); Zigzag on Radial alternates the lines (Raster's rule); the spiral's revolutions
   are one path (each revolution's grid starts at the previous one's last point, G l.21612-21640). */
typedef struct {
    double R, Fo, Rb, s_up, hcell, fp, feed, plunge, lo, zoff;   /* zoff: added to every dropped Z (HST Project's depth copies) */
    HstDrop dp; int has_dp; double pzhi, pzlo, hfe; int flute;   /* PosZHi / PosZLo (mask 0x5b6: PosZLo lowered by the corner radius), the height field's error at the limit, Flute contact 0x16cf (hst_grid_open2) */
    SrHf H; CPaths region; C3Mesh M; int npass, down;
    C3Chk *chk;   /* the check trimming (Spiral, Radial, Project: Edit Passes to Surfaces 0x141f, cam3d_checktrim.c); NULL without checks */
} HstGrid;
/* the common part of gen_raster for the grid plans (the record's refusals, the tool, the drive's height field, the containment) */
static void hst_grid_close(HstGrid *g);
static uint8_t *hst_grid_open2(const McamFile *f, CamOp *op, const char *name, uint32_t *rn, HstGrid *g, int rules) {
    CamParams *p = &op->p; memset(g, 0, sizeof *g);
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "%s here needs the Top tool plane at the origin", name); return NULL; }
    uint8_t *rec = hst_record(f, p, NULL, rn);
    if (!rec || *rn < 0x1760) { free(rec); snprintf(op->error, sizeof op->error, "no %s record (Mastercam's default op 132 is missing)", name); return NULL; }
    #define RD(o) c3d(rec, *rn, o)
    #define RB(o) c3b(rec, *rn, o)
    const char *why = NULL; double alo = RD(0x1248), ahi = RD(0x1250);
    if (RB(0x16d4)) why = "Down Mill / Up Mill (LinkDirectionStyle 2 / 3): the linker is NOT DECODED";
    else if (RB(0x16bf)) why = "the approximate start point is not ported";
    else if (rules != 2 && (fabs(fmin(alo, ahi)) > 1e-6 || fabs(fmax(alo, ahi) - M_PI / 2) > 1e-6)) why = "contact angle limits other than 0 to 90 degrees: the filter is NOT DETERMINED";   /* rules 2 = Scallop's 3D path: the op's angles go to the Shallow Area Detection plan, the passes plan gets 0..90 (mk l.93156-93229) */
    else if (RB(0x16c9) && !rules) why = "Use Z depths: how the passes are cut at the Z limits is NOT DETERMINED";
    else if (rules && RB(0x1730)) why = HST_CONTACT_WHY;
    else if (rules && !RB(0x16cf)) why = HST_NOFLUTE_WHY;
    if (why) { snprintf(op->error, sizeof op->error, "%s: %s", name, why); free(rec); return NULL; }
    double D = p->tool_dia > 0 ? p->tool_dia : RD(0x350); g->R = D * 0.5;
    double Rc = fmin(fmax(p->tool_dia > 0 ? p->corner_r : RD(0x358), 0.0), g->R);
    double sxy = fmax(RD(0x13c0), RD(0x13c8)), sz = RD(0x13c8), ztol = RD(0x12a0), tol = c3_step_tol(rec, *rn, mcam_is_metric(f));   /* StepMachTolerance (c3_step_tol) */
    #undef RD
    #undef RB
    if (!(D > 0)) { snprintf(op->error, sizeof op->error, "tool diameter must be > 0"); free(rec); return NULL; }
    char err[150];
    { char cerr[200]; g->chk = c3_chk_open(f, op, rec, *rn, ztol > 1e-9 ? ztol : tol * 0.1, g->R, Rc, mcam_is_metric(f), cerr, sizeof cerr);
      if (!g->chk && cerr[0]) { snprintf(op->error, sizeof op->error, "%s: %s", name, cerr); free(rec); return NULL; } }
    if (!c3_mesh_op(f, op, ztol > 1e-9 ? ztol : tol * 0.1, &g->M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); free(rec); c3_chk_close(g->chk); g->chk = NULL; return NULL; }
    int flat_tip = Rc + sz <= 0.0;
    g->Rb = flat_tip ? 0.0 : Rc + sz; g->Fo = flat_tip ? g->R + sxy : (g->R - Rc) + sxy - sz; g->s_up = sz;
    g->hcell = fmin(fmax(fmax(ztol, tol) * 4, g->R / 16), g->R / 4);
    /* the Z limits and Flute contact as gen_raster's rule (below, formats/hst3d_port_plan.md 8): the grid plans share the evaluator 18035ec70. rules = Spiral / Radial / Scallop */
    { int zd = c3b(rec, *rn, 0x16c9); double zd1 = c3d(rec, *rn, 0x1238), zd2 = c3d(rec, *rn, 0x1240);
      g->pzhi = g->M.hi[2] + (sz > 0 ? sz : 0) + ztol; g->pzlo = g->M.lo[2] + (sz < 0 ? sz : 0) - ztol - Rc;
      if (zd && rules) { g->pzhi = fmax(zd1, zd2); g->pzlo = fmin(zd1, zd2); }
      g->hfe = g->Rb > 0 ? g->Rb - sqrt(fmax(g->Rb * g->Rb - 0.5 * g->hcell * g->hcell, 0.0)) : 1e-9; g->flute = 0; }
    sr_hf_build_fill(&g->M, g->hcell, g->Fo + g->Rb + g->hcell, (float)g->M.lo[2], &g->H);
    if (rules) { hd_build(&g->dp, &g->M, g->Fo, g->Rb); g->has_dp = 1; { double lim = mcam_is_metric(f) ? 0.1 : 0.003937007874015749; g->hfe = tol < lim ? tol : lim; } }   /* MachTrimZTolerance as gen_raster's (mk l.87957, l.88230, l.88335) */   /* CToolDropPos on the triangles (Spiral, Radial, Scallop); the height field stays for the callers outside this file */
    { char ce[150]; if (!c3_containment(f, op, c3_hst_tcb_type(op), &g->region, ce, sizeof ce)) { snprintf(op->error, sizeof op->error, "%s", ce); free(rec); hst_grid_close(g); return NULL; } }   /* the op's chains as tmptc.tmp holds them */
    if (!c3_tcb_offset(&g->region, rec, *rn, D, fmax(tol * 0.5, 1e-4))) { snprintf(op->error, sizeof op->error, C3_TCB_FAIL); free(rec); hst_grid_close(g); return NULL; }   /* the containment as written to tmptc.tmp (c3_tcb_offset); FUN_180128390's warning 0x127 ends the op */
    g->fp = p->top_z + p->feed_plane; if (g->fp < g->M.hi[2] + sz + p->feed_plane) g->fp = g->M.hi[2] + sz + p->feed_plane;
    g->feed = p->feed > 0 ? p->feed : 100; g->plunge = p->plunge_feed > 0 ? p->plunge_feed : g->feed; g->lo = g->M.lo[2];
    return rec;
}
static uint8_t *hst_grid_open(const McamFile *f, CamOp *op, const char *name, uint32_t *rn, HstGrid *g) { return hst_grid_open2(f, op, name, rn, g, 0); }
static void hst_grid_close(HstGrid *g) { free(g->H.z); if (g->has_dp) hd_free(&g->dp); c3_mesh_free(&g->M); cl_free(&g->region); c3_chk_close(g->chk); g->chk = NULL; }
static double hst_tip(const HstGrid *g, double x, double y) {   /* the drop: exact where hst_grid_open2 built it, else the height field */
    if (g->has_dp) { double z = hd_tip(&g->dp, x, y); return z < -1e29 ? z : z + g->s_up; }
    return sr_hf_tip(&g->H, x, y, g->Fo, g->Rb, g->s_up);
}
/* one pass along the polyline xy[0..n-1] (in its order): dropped every cell, cut where inside the containment (Raster's emission) */
static void hst_grid_pass(HstGrid *g, MB *b, const double *xy, int n) {
    int cap = 64, m = 0; double *s = malloc(sizeof(double) * 3 * cap); char *keep = malloc(cap);
    for (int i = 0; i + 1 < n || (i == 0 && n == 1); i++) {
        double x0 = xy[2 * i], y0 = xy[2 * i + 1], x1 = n > 1 ? xy[2 * i + 2] : x0, y1 = n > 1 ? xy[2 * i + 3] : y0;
        int k = (int)ceil(hypot(x1 - x0, y1 - y0) / g->hcell); if (k < 1) k = 1;
        for (int j = i ? 1 : 0; j <= k; j++) {
            if (m == cap) { cap *= 2; s = realloc(s, sizeof(double) * 3 * cap); keep = realloc(keep, cap); }
            double t = (double)j / k, x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t, z = hst_tip(g, x, y);
            if (!g->has_dp && z < -1e29) z = g->lo;   /* the height field's callers (Pencil, Project): unchanged */
            s[3 * m] = x; s[3 * m + 1] = y; s[3 * m + 2] = z + g->zoff; keep[m] = 1; m++;
        }
        if (n == 1) break;
    }
    if (g->has_dp) { double *zz = malloc(sizeof(double) * (m + 1)); for (int q = 0; q < m; q++) zz[q] = s[3 * q + 2]; hst_z_filter(zz, m, g->pzlo, g->pzhi, g->hfe, keep); free(zz); }   /* the Z window (FUN_180361bd0) */
    int k = 0;
    while (k < m) {
        #define PASS_IN(k) (keep[k] && (!g->region.n || wl_inside(&g->region, s[3 * (k)], s[3 * (k) + 1])))
        if (!PASS_IN(k)) { k++; continue; }
        int a0 = k; while (k < m && PASS_IN(k)) k++;
        if (k - a0 < 2) continue;
        C3Emit em = { b, g->fp, g->plunge, g->feed, &g->down, NULL, NULL, 0 };
        g->npass += c3_chk_emit(g->chk, s + 3 * a0, k - a0, 0, &em);
    }
    #undef PASS_IN
    free(s); free(keep);
}
static int hst_grid_done(HstGrid *g, CamOp *op, MB *b) {
    hst_grid_close(g);
    if (!g->npass || !b->n) { snprintf(op->error, sizeof op->error, "nothing to cut"); return 0; }
    { V3 l = lastp(b); mv(b, 1, l.x, l.y, op->p.clearance > g->fp ? op->p.clearance : g->fp, 0); }
    return 1;
}
/* the plan's radii: RadialFarRad only with "Outer radius" on (0x16e3 = 0); its default otherwise is NOT DECODED */
static int hst_radii(CamOp *op, const uint8_t *rec, uint32_t rn, const char *name, int spiral, double tol, double *close, double *far) {
    *close = c3d(rec, rn, 0x1400); *far = c3d(rec, rn, 0x1408);
    if (c3b(rec, rn, 0x16e3) || (spiral && !(tol * 10.0 < *far - *close))) {
        snprintf(op->error, sizeof op->error, "%s: no outer radius (the plan's default RadialFarRad is NOT DECODED)", name); return 0; }
    return 1;
}
/* Spiral: CSpiralPassesPlan FUN_18037eaf0 (G l.21262-22290): r(t) = close + t step / 2 pi, P(t) = c + (+-cos t r, sin t r) (x negated for
   SpiralClockwise); N = 4 points per revolution, doubled until the chord's midpoint lies within the linearisation tolerance of P at the half
   angle (the start G l.21470-21540; each later point G l.21880-21990: on a miss N doubles and the index becomes 2 i - 1); points until one at or
   past the far radius (the loop condition G l.21991-21992). Other Way: the order reversed (LinkReverseDirection + LinkReverseOrder). */
static int gen_spiral(const McamFile *f, CamOp *op, MB *b) {
    HstGrid g; uint32_t rn = 0; uint8_t *rec = hst_grid_open2(f, op, "Spiral", &rn, &g, 1);
    if (!rec) return 0;
    int metric = mcam_is_metric(f);
    double job = c3_step_tol(rec, rn, metric), close, far;                           /* job +0xa0 (c3_step_tol): SpiralLinearisationTolerance below */
    double step = c3d(rec, rn, 0x1230); if (!(step > 1e-12)) step = metric ? 2.5 : 0.1;
    double cx = c3d(rec, rn, 0x13f0), cy = c3d(rec, rn, 0x13f8);
    int zig = c3b(rec, rn, 0x16bd), other = c3b(rec, rn, 0x16d5), cw = c3b(rec, rn, 0x16df) != 0;
    if (!hst_radii(op, rec, rn, "Spiral", 1, c3d(rec, rn, 0x12a0), &close, &far)) { free(rec); hst_grid_close(&g); return 0; }
    free(rec);
    if (zig) { snprintf(op->error, sizeof op->error, "Spiral: Zigzag (the bidirectional Spiral Linker 0x142c is NOT DECODED)"); hst_grid_close(&g); return 0; }
    if (other) cw = !cw;
    double lim = metric ? 0.01 : 0.01 / 25.4, lt = job < lim ? job : lim, lt2 = lt * lt, k = step / (2 * M_PI), sg = cw ? -1.0 : 1.0;
    #define SP(t, X, Y) do { double r_ = close + (t) * k, a_ = fmod((t), 2 * M_PI); X = cx + sg * cos(a_) * r_; Y = cy + sin(a_) * r_; } while (0)
    int cap = 256, n = 0; double *xy = malloc(sizeof(double) * 2 * cap);
    double px, py, qx, qy, hx, hy; SP(0.0, px, py);
    long N = 4; SP(2 * M_PI / N, qx, qy);
    for (;;) { SP(2 * M_PI / (2 * N), hx, hy); double mx = (px + qx) * 0.5 - hx, my = (py + qy) * 0.5 - hy;
        if (mx * mx + my * my <= lt2 || N > (1L << 24)) break; qx = hx; qy = hy; N *= 2; }
    long i = 1; xy[n++] = px; xy[n++] = py;
    int bad = 0;
    while (close + (double)(i - 1) / N * 2 * M_PI * k < far) {
        double t = (double)i / N * 2 * M_PI; SP(t, qx, qy);
        for (;;) { SP((2.0 * i - 1) / (2.0 * N) * 2 * M_PI, hx, hy); double mx = (px + qx) * 0.5 - hx, my = (py + qy) * 0.5 - hy;
            if (mx * mx + my * my <= lt2 || N > (1L << 24)) break; N *= 2; i = 2 * i - 1; qx = hx; qy = hy; }
        if (n / 2 == cap) { if (cap > (1 << 24)) { bad = 1; break; } cap *= 2; xy = realloc(xy, sizeof(double) * 2 * cap); }
        xy[2 * (n / 2)] = qx; xy[2 * (n / 2) + 1] = qy; n += 2;
        px = qx; py = qy; i++;
        if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); free(xy); hst_grid_close(&g); return 0; }
    }
    #undef SP
    if (bad) { snprintf(op->error, sizeof op->error, "Spiral: too many points"); free(xy); hst_grid_close(&g); return 0; }
    int np = n / 2;
    if (other) for (int a = 0, c = np - 1; a < c; a++, c--) { double tx = xy[2 * a], ty = xy[2 * a + 1]; xy[2 * a] = xy[2 * c]; xy[2 * a + 1] = xy[2 * c + 1]; xy[2 * c] = tx; xy[2 * c + 1] = ty; }
    hst_grid_pass(&g, b, xy, np);
    free(xy);
    return hst_grid_done(&g, op, b);
}
/* Radial: CRadialPassesPlan FUN_180380f90 (G l.22467-22711 + its disassembly 180381198-1803812ae): the angular step 2 atan(step / 2 / (far + e))
   at the far radius; end < start, or end = start, makes a full turn (start -= 2 pi when start >= 0, else end += 2 pi); k = ceil(span / step)
   with the step then span / k, one line fewer for a full 2 pi; line i at start + i step from max(close - e, 0) to far + e, inner to outer. */
static int gen_radial(const McamFile *f, CamOp *op, MB *b) {
    HstGrid g; uint32_t rn = 0; uint8_t *rec = hst_grid_open2(f, op, "Radial", &rn, &g, 1);
    if (!rec) return 0;
    int metric = mcam_is_metric(f);
    double close, far, step = c3d(rec, rn, 0x1230); if (!(step > 1e-12)) step = metric ? 2.5 : 0.1;
    double cx = c3d(rec, rn, 0x13f0), cy = c3d(rec, rn, 0x13f8), a0 = c3d(rec, rn, 0x1410), a1 = c3d(rec, rn, 0x1418);
    int zig = c3b(rec, rn, 0x16bd), other = c3b(rec, rn, 0x16d5);
    if (!hst_radii(op, rec, rn, "Radial", 0, 0, &close, &far)) { free(rec); hst_grid_close(&g); return 0; }
    free(rec);
    double e = 0, r0 = close - e > 0 ? close - e : 0, r1 = far + e, da = atan(step * 0.5 / r1) * 2.0;
    if (!(r1 > r0) || !(da > 0)) { snprintf(op->error, sizeof op->error, "Radial: the outer radius must exceed the inner radius"); hst_grid_close(&g); return 0; }
    if (a1 < a0) { if (a0 >= 0) a0 -= 2 * M_PI; else a1 += 2 * M_PI; }
    if (a1 == a0) { if (a0 >= 0) a0 -= 2 * M_PI; else a1 += 2 * M_PI; }
    double span = a1 - a0, q = span / da; int nl = (int)q;
    if (q - nl != 0.0) { nl++; da = span / nl; }
    if (span == 2 * M_PI) nl--;
    if (nl > 100000) { snprintf(op->error, sizeof op->error, "too many radial lines"); hst_grid_close(&g); return 0; }
    for (int i = 0; i <= nl; i++) {
        if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); hst_grid_close(&g); return 0; }
        double a = a0 + i * da, c = cos(a), s = sin(a), xy[4] = { cx + c * r0, cy + s * r0, cx + c * r1, cy + s * r1 };
        int rev = zig ? (i & 1) : other;
        if (rev) { double t0 = xy[0], t1 = xy[1]; xy[0] = xy[2]; xy[1] = xy[3]; xy[2] = t0; xy[3] = t1; }
        hst_grid_pass(&g, b, xy, 2);
    }
    return hst_grid_done(&g, op, b);
}

/* ------------------------------------------------------------------------------------------------------------------ Scallop */
/* HST Scallop (UI style 3, kernel style 2), the "Constant Stepover Passes from 2D Boundaries" plan 0x140c. Decode: formats/scallop.md
   (cs = decomp/scallop/cs.c, off = decomp/scallop/off.c, mk = decomp/mckernel_all.c). MachConstStepoverHorizontal = Vertical = record
   0x1230 (2.5 / 0.1 when unset, mk l.87913-87934), so the Z ratio H / V = 1. Drive boundaries = the containment, else the Bounding Box
   Plan of the drive offset by |XY stock| + D/2 + 2 x tolerance (FUN_180339740 cs l.3965-3994; mk l.92862-92868). Pass 0 = the boundary;
   pass k + 1 = the 0-level of d3(node, pass k) - H over the weave of tool positions, d3 = the 3D point-segment distance searched to 2H
   (FUN_1803343e0 cs l.794-1881, FUN_180265980 off l.6852-7207, MeasurePointDistance3D FUN_180238070); a pocket ends when no contour is
   left. "Use boundaries as drive curves, then collapse" (0x16e1 = 1, **the default op**): the 3D plan 0x1445 fed by Shallow Area Detection
   0x140a (below, before gen_scallop). Refused: Raster's refusals (hst_grid_open2). Ours: the weave spacing (MachWeaveResolution needs
   ToolShaftRadius, NOT DECODED: here the height field's cell), the node = the tool tip of the height-field drop (centre or tip NOT
   DECODED), the contouring (marching squares; FUN_180262430 / FUN_1802666a0 / FUN_1802669e0 NOT DECODED), the done region (nodes within
   H of an earlier pass) standing in for the sign rules, every loop kept (the island / discard tests NOT DECODED), the order (pass by
   pass, boundary inward, reversed with Expand inside to out [measured]; the pocket spawning LIFO and the Constant Surface Stepover Linker
   0x1420 NOT DECODED), the loop direction (the contour's), the SAFE links. */
static void cl_push(CPaths *ps, CPath P) { ps->p = realloc(ps->p, sizeof(CPath) * (ps->n + 1)); ps->p[ps->n++] = P; }   /* takes P's points */
/* the 0-level of g on an nx x ny grid (origin x0 / y0, spacing h) as closed loops, g >= 0 on the left; returns the loop count */
static int sc_contours(const float *g, int nx, int ny, double x0, double y0, double h, CPaths *out) {
    size_t ne = (size_t)2 * nx * ny; int *nxt = malloc(sizeof(int) * ne); double *ex = malloc(sizeof(double) * 2 * ne);
    for (size_t i = 0; i < ne; i++) nxt[i] = -1;
    #define GV(i, j) g[(size_t)(j) * nx + (i)]
    #define HE(i, j) (2 * ((j) * nx + (i)))          /* edge (i,j)-(i+1,j) */
    #define VE(i, j) (2 * ((j) * nx + (i)) + 1)      /* edge (i,j)-(i,j+1) */
    for (int j = 0; j + 1 < ny; j++) for (int i = 0; i + 1 < nx; i++) {
        float c[4] = { GV(i, j), GV(i + 1, j), GV(i + 1, j + 1), GV(i, j + 1) };
        int pos[4], np = 0; for (int k = 0; k < 4; k++) { pos[k] = c[k] >= 0; np += pos[k]; }
        if (np == 0 || np == 4) continue;
        double cx[4] = { x0 + i * h, x0 + (i + 1) * h, x0 + (i + 1) * h, x0 + i * h }, cy[4] = { y0 + j * h, y0 + j * h, y0 + (j + 1) * h, y0 + (j + 1) * h };
        int eid[4] = { HE(i, j), VE(i + 1, j), HE(i, j + 1), VE(i, j) };   /* edge k joins corner k and k + 1 */
        for (int k = 0; k < 4; k++) { int a = k, b = (k + 1) & 3; if (pos[a] == pos[b]) continue;
            double t = c[a] / (c[a] - c[b]); ex[2 * eid[k]] = cx[a] + (cx[b] - cx[a]) * t; ex[2 * eid[k] + 1] = cy[a] + (cy[b] - cy[a]) * t; }
        int segs[2][2], ns = 0, iso[2] = { -1, -1 };
        if (np == 2 && pos[0] == pos[2]) {   /* saddle: the centre's sign joins its own corners */
            int cpos = (c[0] + c[1] + c[2] + c[3]) * 0.25f >= 0;
            int k0 = cpos == pos[0] ? 1 : 0;   /* the corners cut off: those of the other sign (k0, k0 + 2) */
            for (int q = 0; q < 2; q++) { int k = k0 + 2 * q; segs[ns][0] = eid[(k + 3) & 3]; segs[ns][1] = eid[k]; iso[ns] = k; ns++; }
        } else {
            int s0 = -1, s1 = -1; for (int k = 0; k < 4; k++) if (pos[k] != pos[(k + 1) & 3]) { if (s0 < 0) s0 = eid[k]; else s1 = eid[k]; }
            segs[0][0] = s0; segs[0][1] = s1; ns = 1;
        }
        for (int q = 0; q < ns; q++) {
            int a = segs[q][0], b = segs[q][1];
            double ax = ex[2 * a], ay = ex[2 * a + 1], bx = ex[2 * b], by = ex[2 * b + 1];
            int k = iso[q] >= 0 ? iso[q] : -1; double sgn = 1;
            if (k < 0) { for (int m = 0; m < 4; m++) if (pos[m]) { k = m; break; } }
            else sgn = pos[k] ? 1 : -1;
            double cr = (bx - ax) * (cy[k] - ay) - (by - ay) * (cx[k] - ax);
            if (cr * sgn < 0) { int t = a; a = b; b = t; }
            nxt[a] = b;
        }
    }
    int nl = 0;
    for (size_t e = 0; e < ne; e++) {
        if (nxt[e] < 0) continue;
        CPath P = { 0 }; int cap = 64; P.xy = malloc(sizeof(double) * 2 * cap);
        int cur = (int)e;
        while (cur >= 0 && nxt[cur] >= 0) {
            if (P.n == cap) { cap *= 2; P.xy = realloc(P.xy, sizeof(double) * 2 * cap); }
            P.xy[2 * P.n] = ex[2 * cur]; P.xy[2 * P.n + 1] = ex[2 * cur + 1]; P.n++;
            int nn = nxt[cur]; nxt[cur] = -1; cur = nn;
        }
        if (P.n >= 3) { cl_push(out, P); nl++; } else free(P.xy);
    }
    #undef GV
    #undef HE
    #undef VE
    free(nxt); free(ex);
    return nl;
}
/* the Z limits on Scallop: the node field tz stays unclamped; Mastercam's contours are cut at PosZHi / PosZLo - Eva +0x140 inside the weave (scallop/off.c l.4339-4342), here the passes go through hst_z_filter in hst_grid_pass */
/* Scallop's 3D boundaries path ("Use boundaries as drive curves, then collapse", record 0x16e1 = 1; formats/scallop.md 10). mckernel's macro
   (FUN_18007d7c8 mk l.93155-93240) feeds the "// Shallow Area Detection Plan" 0x140a (params FUN_180079814 mk l.90448-90592: the tool, the stock,
   PosZ, ContactAngleLo / Hi = the op's 0x1248 / 0x1250 clamped to 0..pi/2) into DriveBoundaries of "Constant Stepover Passes from 3D Boundaries"
   0x1445, whose own contact angles are 0..90 (mode 2). 0x140a = CDetectShallowAreasPlan (ctor FUN_18031dcf0; its defaults FUN_18030fc80:
   MachConstStepover 0, MachConstNumberOfOffsets 1, AreaDetectContactNormalChange 0.3, MinDiameter 0x1b49 = 1.0), evaluator EvaAreaDetectShallowPlan
   (vftable 180787b68, [1] FUN_18031dd90 -> FUN_180317080 toolbox, FUN_180316750 weave, FUN_180317200 boundaries; decomp/scallop3d/ad.c): the weave over
   the constraint area is classified by the tool's contact (FUN_180263860 / FUN_180262430; the contact test FUN_1802f3a80: the window Eva +0x678..+0x680
   from ContactAngleLo / Hi / Tol, FUN_1802f2340, and the Z window +0x6c0), its loops grouped (FUN_1802666a0 / FUN_1802669e0), the groups whose
   MeasureCurveDiameter (FUN_18004cf60) is below MinDiameter deleted ("Deleting N small profiles before offset", disassembly 1803178a3-1803178f2), and each
   loop output with z + ToolThicknessZ (Eva +0x140). The 3D plan's first offset of each boundary is at r = Mach3DBoundaryTolerance 0x1bb1 (0.1,
   FUN_18033a950 cs l.4649-4655), retried at r x 1.5 up to 4 times (FUN_18033aaf0 cs l.4851-4943, the log lines cs l.5536 / l.5579), then the
   stepover H as the 2D plan; the boundary itself is not a pass. McHmm lengths are mm (the macro values are scaled by job +0x80 = 25.4 on an inch part,
   FUN_1800edd8c / FUN_18007bb48 mk l.91625-91633), so MinDiameter and Mach3DBoundaryTolerance are mm [measured: MULTIAXIS ROUGHING FALSE FLOOR op 6's
   first pass lies 0.002-0.003 in inside the contact boundary, not 0.1 in]. REFUSED until the weave cell classification (FUN_18026c540, decoded in
   formats/scallop.md 10.6, needs the CWeave engine) is ported: the decoded parts (the angle window, MinDiameter, Mach3DBoundaryTolerance, the first
   offset and its retries in the passes loop) wait for them. */
typedef struct { double m[9], o[3]; } ScPlane;
static void sc_test_plane(HstGrid *g, const ScPlane *P, double ztol) {   /* ours (testing, MCAM_SC_PLANE): the drive into a stored op's tool plane, v = M (w - o); the job range in it */
    for (int i = 0; i < 3 * g->M.n; i++) { double *w = g->M.t + 3 * i, d[3] = { w[0] - P->o[0], w[1] - P->o[1], w[2] - P->o[2] };
        for (int r = 0; r < 3; r++) w[r] = P->m[3 * r] * d[0] + P->m[3 * r + 1] * d[1] + P->m[3 * r + 2] * d[2]; }
    for (int a = 0; a < 3; a++) { g->M.lo[a] = 1e300; g->M.hi[a] = -1e300; }
    for (int i = 0; i < 3 * g->M.n; i++) for (int a = 0; a < 3; a++) { g->M.lo[a] = fmin(g->M.lo[a], g->M.t[3 * i + a]); g->M.hi[a] = fmax(g->M.hi[a], g->M.t[3 * i + a]); }
    free(g->H.z); memset(&g->H, 0, sizeof g->H); sr_hf_build_fill(&g->M, g->hcell, g->Fo + g->Rb + g->hcell, (float)g->M.lo[2], &g->H);
    if (g->has_dp) { hd_free(&g->dp); hd_build(&g->dp, &g->M, g->Fo, g->Rb); }
    g->pzhi = g->M.hi[2] + (g->s_up > 0 ? g->s_up : 0) + ztol; g->pzlo = g->M.lo[2] + (g->s_up < 0 ? g->s_up : 0) - ztol - (g->Rb - g->s_up); g->lo = g->M.lo[2];   /* as hst_grid_open2 */
}
static int gen_scallop(const McamFile *f, CamOp *op, MB *b) {
    int collapse = 0; { uint32_t rn0 = 0; uint8_t *r0 = hst_record(f, &op->p, NULL, &rn0); if (r0) collapse = c3b(r0, rn0, 0x16e1); free(r0); }
    HstGrid g; uint32_t rn = 0; uint8_t *rec = hst_grid_open2(f, op, "Scallop", &rn, &g, collapse ? 2 : 1);
    if (!rec) return 0;
    int metric = mcam_is_metric(f);
    double H = c3d(rec, rn, 0x1230); if (!(H > 1e-12)) H = metric ? 2.5 : 0.1;
    double sxy = fmax(c3d(rec, rn, 0x13c0), c3d(rec, rn, 0x13c8)), tol = c3d(rec, rn, 0x12a0);
    int outward = c3b(rec, rn, 0x16db) == 0;   /* Expand inside to out = (0x16db == 0) (page 1328 control 1309) */
    double alo = c3d(rec, rn, 0x1248), ahi = c3d(rec, rn, 0x1250);
    free(rec);
    { const char *e = getenv("MCAM_SC_PLANE"); double v[12];
      if (e && sscanf(e, "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", v, v + 1, v + 2, v + 3, v + 4, v + 5, v + 6, v + 7, v + 8, v + 9, v + 10, v + 11) == 12) {
          ScPlane tp; memcpy(tp.m, v, sizeof tp.m); memcpy(tp.o, v + 9, sizeof tp.o); sc_test_plane(&g, &tp, tol);
          /* the containment: a closed spline given by its idn (MCAM_SC_CONTAIN; cam_generate takes no splines here) sampled in 3D into the plane, then offset */
          CPaths raw = { 0 }; int cid = getenv("MCAM_SC_CONTAIN") ? atoi(getenv("MCAM_SC_CONTAIN")) : 0;
          for (int c = 0; cid && c < f->nents; c++) { if (f->ents[c].idn != cid || f->ents[c].type != E_SPLINE) continue;
              const CurveRec *cr = ent_crv(f, &f->ents[c]); if (!cr) continue;
              double t1, t2, o3[3][3]; mcs_spline_t1t2(cr, &t1, &t2); CPath Q = { malloc(sizeof(double) * 2 * 4000), 4000 };
              for (int k = 0; k < 4000; k++) { mcs_spline_eval_d(cr, t1 + (t2 - t1) * k / 4000, 0, o3); double d[3] = { o3[0][0] - tp.o[0], o3[0][1] - tp.o[1], o3[0][2] - tp.o[2] };
                  Q.xy[2 * k] = tp.m[0] * d[0] + tp.m[1] * d[1] + tp.m[2] * d[2]; Q.xy[2 * k + 1] = tp.m[3] * d[0] + tp.m[4] * d[1] + tp.m[5] * d[2]; }
              cl_push(&raw, Q); }
          if (raw.n) { uint32_t rn1 = 0; uint8_t *r1 = hst_record(f, &op->p, NULL, &rn1); cl_free(&g.region); g.region = cl_union_evenodd(&raw);
              if (r1) c3_tcb_offset(&g.region, r1, rn1, 2 * g.R, fmax(tol * 0.5, 1e-4)); free(r1); }
          cl_free(&raw); } }
    /* the drive boundaries: the containment, else the drive's box offset by |XY stock| + D/2 + 2 tol (3D path: the constraint area of the detection) */
    CPaths bnd = { 0 };
    if (g.region.n) { for (int i = 0; i < g.region.n; i++) { CPath P = { 0 }; P.n = g.region.p[i].n; P.xy = malloc(sizeof(double) * 2 * P.n); memcpy(P.xy, g.region.p[i].xy, sizeof(double) * 2 * P.n); cl_push(&bnd, P); } }
    else { double o = fabs(sxy) + g.R + 2 * tol, x0 = g.M.lo[0] - o, y0 = g.M.lo[1] - o, x1 = g.M.hi[0] + o, y1 = g.M.hi[1] + o;
        CPath P = { 0 }; P.n = 4; P.xy = malloc(sizeof(double) * 8); double q[8] = { x0, y0, x1, y0, x1, y1, x0, y1 }; memcpy(P.xy, q, sizeof q); cl_push(&bnd, P); }
    double bx0 = 1e300, by0 = 1e300, bx1 = -1e300, by1 = -1e300;
    for (int i = 0; i < bnd.n; i++) for (int k = 0; k < bnd.p[i].n; k++) { bx0 = fmin(bx0, bnd.p[i].xy[2 * k]); bx1 = fmax(bx1, bnd.p[i].xy[2 * k]); by0 = fmin(by0, bnd.p[i].xy[2 * k + 1]); by1 = fmax(by1, bnd.p[i].xy[2 * k + 1]); }
    double h = g.hcell; int nx = (int)ceil((bx1 - bx0) / h) + 3, ny = (int)ceil((by1 - by0) / h) + 3; double x0 = bx0 - h, y0 = by0 - h;
    if ((double)nx * ny > 2.5e7) { snprintf(op->error, sizeof op->error, "Scallop: the weave is too large (%d x %d)", nx, ny); cl_free(&bnd); hst_grid_close(&g); return 0; }
    size_t nn = (size_t)nx * ny; float *tz = malloc(sizeof(float) * nn), *dist = malloc(sizeof(float) * nn), *gv = malloc(sizeof(float) * nn); char *done = calloc(nn, 1);
    for (int j = 0; j < ny; j++) for (int i = 0; i < nx; i++) { size_t k = (size_t)j * nx + i; double x = x0 + i * h, y = y0 + j * h;
        double z = hst_tip(&g, x, y); tz[k] = (float)(z < -1e29 ? g.lo : z);
        if (collapse) { gv[k] = (float)fmax(z, -1e30); continue; }   /* the raw drop (no contact: -1e30), classified below */
        done[k] = i == 0 || j == 0 || i == nx - 1 || j == ny - 1 || !wl_inside(&bnd, x, y); }
    double r3 = H;   /* the first step: the 2D plan's pass 0 is the boundary itself, its next pass at H */
    if (collapse) {
        /* Shallow Area Detection 0x140a: the angles clamped as FUN_180079814 (mk l.90560-90581); the cosine window of FUN_1802f2340 with the ContactAngleTol
           default (FUN_18030a730; formats/hst3d_port_plan.md 7 "Contact angle filter") */
        double lo = fmin(alo, ahi), hi = fmax(alo, ahi); if (hi > M_PI / 2) hi = M_PI / 2; if (hi < 0) hi = 0; if (lo > M_PI / 2) lo = M_PI / 2; if (lo < 0) lo = 0;
        double lod = round(lo * 57.29577951308233 * 1e9) / 1e9, hid = round(hi * 57.29577951308233 * 1e9) / 1e9;   /* degrees, written to the macro with %19.9f (formats/hst3d.md) */
        lo = lod * M_PI / 180; hi = hid * M_PI / 180;
        double tdeg = fmin(lod > 0 ? fmin(lod / 10, 1.0) : 1.0, hid < 90 ? fmin((90 - hid) / 10, 1.0) : 1.0), at = tdeg * M_PI / 180;
        double czlo = hi <= M_PI / 2 - at ? cos(hi) : -0.1, czhi = lo >= at ? cos(lo) : 1.1; int angf = czlo > -0.1 || czhi < 1.1;
        if (angf && g.Rb <= 0) { snprintf(op->error, sizeof op->error, "Scallop: contact angle limits with a flat tool (the contact normal at the tool's edge is NOT DECODED)"); free(tz); free(dist); free(gv); free(done); cl_free(&bnd); hst_grid_close(&g); return 0; }
        const double mm = metric ? 1.0 : 1 / 25.4;   /* McHmm works in mm: job +0x80 = 25.4 for an inch part (FUN_18007bb48 mk l.91625-91633, FUN_1800edd8c) */
        double mind = 1.0 * mm; r3 = 0.1 * mm;          /* MinDiameter 0x1b49 (FUN_18030fc80), Mach3DBoundaryTolerance 0x1bb1 (FUN_18033a950) */
        (void)mind; (void)czlo; (void)czhi;
        /* NOT PORTED: the weave cells' classification (FUN_180262430 -> FUN_18026c540: the sign change of the contact test FUN_1802f3a80 between the
           CWeave's fibre crossings, formats/scallop.md 10.6) needs the CWeave engine and the drop's contact records, which this port does not have (the
           2D path stands in for them). MeasureCurveDiameter (FUN_18004cb90) is decoded: for line loops the largest XY vertex distance. Refused, no
           stand-in (user, 2026-11-13: decode first) */
        snprintf(op->error, sizeof op->error, "Scallop: boundaries as drive curves (0x16e1): Shallow Area Detection needs the CWeave cell test (FUN_18026c540), NOT PORTED - switch it off");
        free(tz); free(dist); free(gv); free(done); cl_free(&bnd); hst_grid_close(&g); return 0;
    }
    /* the passes: pass 0 the boundary, then the offsets */
    CPaths pass = bnd; memset(&bnd, 0, sizeof bnd);
    int it = 0, cancelled = 0, tries = 0; CPaths all = { 0 };   /* every loop in generation order */
    for (; pass.n && it < 100000; it++) {
        if (mcce_abort_get()) { cancelled = 1; break; }
        if (!collapse || it > 0) for (int i = 0; i < pass.n; i++) { CPath P = { 0 }; P.n = pass.p[i].n; P.xy = malloc(sizeof(double) * 2 * P.n); memcpy(P.xy, pass.p[i].xy, sizeof(double) * 2 * P.n); cl_push(&all, P); }
        double step = it == 0 ? r3 : H;
        /* d3 to this pass (searched to 2H; 2 x the step for a larger first offset) */
        for (size_t k = 0; k < nn; k++) dist[k] = 1e30f;
        double rr = 2 * fmax(H, step);
        for (int i = 0; i < pass.n; i++) { CPath *P = &pass.p[i];
            for (int s = 0; s < P->n; s++) {
                double ax = P->xy[2 * s], ay = P->xy[2 * s + 1], cx = P->xy[2 * ((s + 1) % P->n)], cy = P->xy[2 * ((s + 1) % P->n) + 1];
                double az = hst_tip(&g, ax, ay), cz = hst_tip(&g, cx, cy);
                if (az < -1e29) az = g.lo;
                if (cz < -1e29) cz = g.lo;
                int i0 = (int)floor((fmin(ax, cx) - rr - x0) / h), i1 = (int)ceil((fmax(ax, cx) + rr - x0) / h);
                int j0 = (int)floor((fmin(ay, cy) - rr - y0) / h), j1 = (int)ceil((fmax(ay, cy) + rr - y0) / h);
                if (i0 < 0) i0 = 0;
                if (j0 < 0) j0 = 0;
                if (i1 > nx - 1) i1 = nx - 1;
                if (j1 > ny - 1) j1 = ny - 1;
                double dx = cx - ax, dy = cy - ay, dz = cz - az, L2 = dx * dx + dy * dy + dz * dz;
                for (int jj = j0; jj <= j1; jj++) for (int ii = i0; ii <= i1; ii++) { size_t k = (size_t)jj * nx + ii; if (done[k]) continue;
                    double px = x0 + ii * h - ax, py = y0 + jj * h - ay, pz = tz[k] - az, t = L2 > 0 ? (px * dx + py * dy + pz * dz) / L2 : 0;
                    t = t < 0 ? 0 : t > 1 ? 1 : t; double qx = px - t * dx, qy = py - t * dy, qz = pz - t * dz, d = sqrt(qx * qx + qy * qy + qz * qz);
                    if (d < dist[k]) dist[k] = (float)d; }
            } }
        int left = 0, grew = 0;
        for (size_t k = 0; k < nn; k++) { if (done[k]) gv[k] = (float)-step; else { gv[k] = dist[k] < 1e29f ? (float)(dist[k] - step) : (float)step; if (gv[k] < 0) grew = 1; else left = 1; } }
        if (collapse && it == 0 && !left && ++tries < 4) { r3 *= 1.5; it--; continue; }   /* "First offset failed. Retry with rad = " r x 1.5, 4 attempts (FUN_18033aaf0; cs l.5536 / l.5579) */
        for (size_t k = 0; k < nn; k++) if (!done[k] && gv[k] < 0) done[k] = 1;
        /* ours: a node next to the done region with nothing of this pass within 2H lies across a wall (the tip field jumps in Z): it joins
           the front, so the wall's foot becomes part of the next pass (Mastercam starts a new pocket there, its first pass the boundary
           itself - the pocket split and FUN_18033aaf0's first offset are not ported) */
        for (int j = 1; j + 1 < ny; j++) for (int i = 1; i + 1 < nx; i++) { size_t k = (size_t)j * nx + i;
            if (done[k] || dist[k] < 1e29f) continue;
            if (gv[k - 1] < 0 || gv[k + 1] < 0 || gv[k - nx] < 0 || gv[k + nx] < 0) { gv[k] = (float)(-0.5 * h); grew = 2; } }
        for (size_t k = 0; k < nn; k++) if (!done[k] && gv[k] < 0) done[k] = 1;
        cl_free(&pass);
        if (!left || !grew) break;
        sc_contours(gv, nx, ny, x0, y0, h, &pass);
    }
    cl_free(&pass); free(tz); free(dist); free(gv); free(done);
    if (cancelled) { snprintf(op->error, sizeof op->error, "cancelled"); cl_free(&all); hst_grid_close(&g); return 0; }
    /* cut the loops closed: boundary inward, or the reverse with Expand inside to out (the stored TUTORIAL 2 SWARF_METRIC op 4, which has it,
       runs from the centre out [measured]; the linker's own ordering is NOT DECODED) */
    for (int q = 0; q < all.n; q++) {
        CPath *P = &all.p[outward ? all.n - 1 - q : q]; double *xy = malloc(sizeof(double) * 2 * (P->n + 1)); memcpy(xy, P->xy, sizeof(double) * 2 * P->n);
        xy[2 * P->n] = P->xy[0]; xy[2 * P->n + 1] = P->xy[1]; hst_grid_pass(&g, b, xy, P->n + 1); free(xy); }
    cl_free(&all);
    return hst_grid_done(&g, op, b);
}

/* ------------------------------------------------------------------------------------------------------------------ HST Project */
/* HST Project (UI style 15, kernel style 10). Decode: formats/hst_project_hybrid.md 1 (mk = decomp/mckernel_all.c, ms = decomp/mcmsurf_all.c,
   G = decomp/raster/grid.c, decomp/proj/). Page 1517: projection type record 0x165a (0 NCI, 1 Curves, 2 Points), source op 0x157c, number of
   depth cuts - 1 = NumberOfOffsets 0x1558, stepdown 0x13b8, Order by depth 0x165e (ByOffset = !it).
   - NCI (1.3.1): the source op's toolpath, every G0 ending a pass (HlaToApt ms l.347968-348146); the boundary plan keeps XY only (1.3.2 kernel
     read). Here: the source op's cut polylines (its rapids split them) from this part's toolpath points.
   - Curves (1.3.2): each chain of the op linearised, 2D, open or closed (closed: back to its start).
   - Each boundary is one grid pass dropped onto the drive (EvaBoundaryPassesPlan FUN_180382210 -> FUN_180382a30, then the grid evaluator
     18035ec70: hst_grid_pass, as Raster).
   - Depth cuts (1.5.4, FUN_18031f660): the passes plus NumberOfOffsets copies raised k x stepdown, k = n .. 0 (top first); ByOffset (default):
     each pass top to bottom before the next; else level by level.
   Points (decoded per-point lambda FUN_18001a470, see below). Refused: Raster's (hst_grid_open). Ours: the curves' linearisation (the op's tolerance; the
   msurf_cut_angle frame NOT DECODED: Top, angle 0), Curves mode without a containment (this program's op has one chain list: the curves), the
   pass order (the adjacency order EvaFragAdjacency NOT DECODED: the source order), the arcs of the source NCI as this program's toolpath
   points, the SAFE links (the Project Boundary linker 0x142e NOT DECODED). */
static void hst_point_pass(HstGrid *g, MB *b, double x, double y, double z) {   /* a one-position pass: plunge to it; the SAFE link (ours, the linker 0x147b is NOT DECODED) */
    if (g->chk) { double P[3] = { x, y, z }; if (c3chk_in(g->chk, P)) return; }   /* the check trimming of a one-position pass (c3_chk_trim) */
    if (g->down) { V3 l = lastp(b); mv(b, 1, l.x, l.y, g->fp, 0); }
    mv(b, 1, x, y, g->fp, 0); mv(b, 0, x, y, z, g->plunge); g->down = 1; g->npass++;
}
static int gen_hstproject(const McamFile *f, CamOp *op, MB *b) {
    HstGrid g; uint32_t rn = 0; uint8_t *rec = hst_grid_open(f, op, "Project", &rn, &g);
    if (!rec) return 0;
    int mode = (int16_t)(c3b(rec, rn, 0x165a) | c3b(rec, rn, 0x165b) << 8), byoff = !(c3b(rec, rn, 0x165e) | c3b(rec, rn, 0x165f));
    int32_t src = 0, noff = 0; if (rn >= 0x1580) { memcpy(&src, rec + 0x157c, 4); memcpy(&noff, rec + 0x1558, 4); }
    if (noff < 0 || noff > 1000) noff = 0;
    double ptol = c3d(rec, rn, 0x12a0), pRc = op->p.tool_dia > 0 ? op->p.corner_r : c3d(rec, rn, 0x358);   /* Points: PosZHi / PosZLo below */
    double sd = c3d(rec, rn, 0x13b8);
    free(rec);
    if (mode == 2) {   /* Points: EvaPointPassesPlan FUN_180013ae0 -> FUN_180014050; per point the lambda FUN_18001a470 (DecompAt on a copy of ghidra_hmm; not kept in decomp/): inside the ConstraintBoundaries
                          (MeasureInterior2D, EnableTrimToBoundaries), the tool box drop FUN_180222920, the contact test FUN_1802f3a80, z + Eva+0x140 (ToolThicknessZ) within PosZLo .. PosZHi -> a
                          one-position pass FUN_180014810 (CTessToolpathPoint); depth cuts as the other modes (FUN_18031f660 accepts 0x147a). Ours: which points (mask 0x40 of GetConvertProjectPts
                          NOT DECODED: the op's chained points), the drop on our height field, the plunge / retract link (the Project Point Linker 0x147b is NOT DECODED). Unverified: no stored style 15 op. */
        int nout = 0;
        int nt = 0; for (int i = 0; i < op->nchains; i++) nt += op->chains[i].n;
        double *pts = malloc(sizeof(double) * 3 * (nt + 1)); int np = 0;
        for (int i = 0; i < op->nchains; i++) for (int q = 0; q < op->chains[i].n; q++) {
            int ei = op->chains[i].ent[q]; if (ei < 0 || ei >= f->nents || f->ents[ei].type != E_POINT) continue;
            double x = f->ents[ei].a.x, y = f->ents[ei].a.y;
            if (g.region.n && !wl_inside(&g.region, x, y)) continue;          /* MeasureInterior2D */
            double z0 = sr_hf_tip(&g.H, x, y, g.Fo, g.Rb, g.s_up);
            /* FUN_18001a470: the drop FUN_180222920 at the toolbox floor +0x450 (nothing under the tool stays at the floor, the first test z > floor), FUN_1802f3a80 >= 0 (the Z window +0x6c0 and the contact-nz
               window +0x678; the segment filter +0x6e0 and the holder checks are not ported), and z + Eva+0x140 in [PosZLo, PosZHi]. Ours: the Z / angle windows hold at the limits hst_grid_open accepts */
            if (z0 < -1e29) continue;                                         /* no contact */
            { double zl = g.M.lo[2] - ptol - pRc + fmin(g.s_up, 0.0), zh = g.M.hi[2] + fmax(g.s_up, 0.0) + ptol;   /* PosZLo / PosZHi as hst3d_port_plan.md 6.7, style 10 in the mask 0x5b6 */
              if (z0 < zl || z0 > zh) continue; }
            pts[3 * np] = x; pts[3 * np + 1] = y; pts[3 * np + 2] = z0; np++;
        }
        if (byoff) { for (int i = 0; i < np; i++) for (int r = noff; r >= 0; r--) { hst_point_pass(&g, b, pts[3 * i], pts[3 * i + 1], pts[3 * i + 2] + r * sd); nout++; } }
        else { for (int r = noff; r >= 0; r--) for (int i = 0; i < np; i++) { hst_point_pass(&g, b, pts[3 * i], pts[3 * i + 1], pts[3 * i + 2] + r * sd); nout++; } }
        free(pts);
        if (!nout) { snprintf(op->error, sizeof op->error, "Project: Points - select the points to project (none lies on the drive inside the containment)"); hst_grid_close(&g); return 0; }
        return hst_grid_done(&g, op, b);
    }
    if (mode != 0 && mode != 1) { snprintf(op->error, sizeof op->error, "Project: projection type %d not known", mode); hst_grid_close(&g); return 0; }
    CPaths bnd = { 0 };
    if (mode == 1) {   /* Curves: the op's chains, open or closed */
        cl_free(&g.region);
        for (int i = 0; i < op->nchains; i++) { double *xy, z; int n = cam_chain_polyline(f, &op->chains[i], op->p.tol, &xy, &z);
            if (n < 2) { free(xy); continue; }
            CPath P = { 0 }; P.n = n + (op->chains[i].closed ? 1 : 0); P.xy = malloc(sizeof(double) * 2 * P.n); memcpy(P.xy, xy, sizeof(double) * 2 * n);
            if (op->chains[i].closed) { P.xy[2 * n] = xy[0]; P.xy[2 * n + 1] = xy[1]; }
            free(xy); cl_push(&bnd, P); }
        if (!bnd.n) { snprintf(op->error, sizeof op->error, "Project: Curves - select the curves to project"); hst_grid_close(&g); return 0; }
    } else {           /* NCI: the source op's cut runs */
        const Op *so = NULL; for (int i = 0; src > 0 && i < f->nops; i++) if (f->ops[i].id == src) so = &f->ops[i];
        if (!so) { snprintf(op->error, sizeof op->error, "Project: NCI - choose the source operation (Cut Parameters, record 0x157c = %d is not an op of this part)", src); hst_grid_close(&g); return 0; }
        for (int j = 0; j < so->npolys; j++) { const Poly *q = &f->polys[so->first_poly + j]; if (q->kind != K_CUT || q->count < 2) continue;
            CPath P = { 0 }; P.n = q->count; P.xy = malloc(sizeof(double) * 2 * P.n);
            for (int k = 0; k < q->count; k++) { P.xy[2 * k] = f->pts[q->first + k].x; P.xy[2 * k + 1] = f->pts[q->first + k].y; }
            cl_push(&bnd, P); }
        if (!bnd.n) { snprintf(op->error, sizeof op->error, "Project: the source operation has no toolpath"); hst_grid_close(&g); return 0; }
    }
    if (byoff) { for (int i = 0; i < bnd.n; i++) for (int k = noff; k >= 0; k--) { g.zoff = k * sd; hst_grid_pass(&g, b, bnd.p[i].xy, bnd.p[i].n); } }
    else { for (int k = noff; k >= 0; k--) { g.zoff = k * sd; for (int i = 0; i < bnd.n; i++) hst_grid_pass(&g, b, bnd.p[i].xy, bnd.p[i].n); } }
    g.zoff = 0; cl_free(&bnd);
    return hst_grid_done(&g, op, b);
}
