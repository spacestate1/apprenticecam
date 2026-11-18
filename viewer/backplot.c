/* backplot - classic Backplot state (../formats/backplot.md). Section numbers refer to that spec. */
#include "platform.h"
#include "backplot.h"
#include <math.h>

static int vis_n(const Backplot *b) { return b->vis1 - b->vis0; }

void bp_init(Backplot *b, NcMotion *m, int n) {
    memset(b, 0, sizeof *b);
    b->m = m; b->n = n;
    b->cum = xcalloc((size_t)n + 1, sizeof(double));
    for (int i = 0; i < n; i++) b->cum[i + 1] = b->cum[i] + (m[i].len > 0 && isfinite(m[i].len) ? m[i].len : 0);
    b->total = b->cum[n];
    b->vis0 = 0; b->vis1 = n;
    b->flags[1] = 1; b->flags[0xd] = 1;              /* defaults FUN_66d80400: byte 1 = 1, Run (0xd) = 1, rest 0 */
    b->interp_dist = 0.05;                           /* ctor default (bp_interpolate_distance) until the config is read */
    b->pos = 0; b->dist = n ? b->cum[1] : 0;
    bp_compute_stops(b);
}
void bp_free(Backplot *b) { free(b->m); free(b->cum); free(b->stops); memset(b, 0, sizeof *b); }

void bp_set_visible(Backplot *b, int v0, int v1) {   /* Operation List selection -> visible ranges (section 8) */
    if (v0 < 0) v0 = 0; if (v1 > b->n) v1 = b->n; if (v1 < v0) v1 = v0;
    b->vis0 = v0; b->vis1 = v1;
    bp_compute_stops(b);
    bp_seek_motion(b, v1 > v0 ? v1 - 1 : v0);         /* "the position jumps to the last motion of the selection" */
}

void bp_seek_motion(Backplot *b, int i) {             /* slider position -> end of that motion (section 2.2) */
    if (!b->n) return;
    if (i < 0) i = 0; if (i >= b->n) i = b->n - 1;
    b->pos = i; b->dist = b->cum[i + 1];
}
void bp_seek_dist(Backplot *b, double d) {            /* a distance along the path (play / interpolated steps) */
    if (!b->n) return;
    if (d < 0) d = 0; if (d > b->total) d = b->total;
    int lo = 0, hi = b->n - 1;                         /* the motion containing d */
    while (lo < hi) { int mid = (lo + hi) / 2; if (b->cum[mid + 1] < d) lo = mid + 1; else hi = mid; }
    b->pos = lo; b->dist = d;
}
void bp_point(const Backplot *b, double *p) {
    if (!b->n) { p[0] = p[1] = p[2] = 0; return; }
    const NcMotion *m = &b->m[b->pos];
    double L = b->cum[b->pos + 1] - b->cum[b->pos], t = L > 0 ? (b->dist - b->cum[b->pos]) / L : 1;
    nc_motion_point(m, t < 0 ? 0 : t > 1 ? 1 : t, p);
}

/* FUN_66d84250(engine, mode, count) (section 2.5): one motion per step, or count x distance with interpolation on */
void bp_step(Backplot *b, int mode, int count) {
    if (!b->n || vis_n(b) <= 0) return;
    switch (mode) {
    case BP_FIRST: bp_seek_motion(b, b->vis0); break;
    case BP_LAST: bp_seek_motion(b, b->vis1 - 1); break;
    case BP_BACK: case BP_FWD: {
        int dir = mode == BP_FWD ? 1 : -1;
        if (b->interp && b->interp_dist > 0) {         /* FUN_66d627c0: along the path */
            double d = b->dist + dir * count * b->interp_dist, lo = b->cum[b->vis0], hi = b->cum[b->vis1];
            bp_seek_dist(b, d < lo ? lo : d > hi ? hi : d);
        } else {
            int i = b->pos + dir * count;
            if (i < b->vis0) i = b->vis0; if (i > b->vis1 - 1) i = b->vis1 - 1;
            bp_seek_motion(b, i);
        }
        break; }
    case BP_PREV_STOP: case BP_NEXT_STOP: {            /* FUN_66d84030: the stop list, lower bound */
        int best = mode == BP_NEXT_STOP ? b->vis1 - 1 : b->vis0;
        for (int k = 0; k < b->nstops; k++) {
            int s = b->stops[k];
            if (mode == BP_NEXT_STOP && s > b->pos) { best = s; break; }
            if (mode == BP_PREV_STOP && s < b->pos) best = s;
        }
        bp_seek_motion(b, best);
        break; }
    }
}

/* FUN_66d62070 (section 3): reset -> stop at the first motion; then per operation / tool / motion end point */
void bp_compute_stops(Backplot *b) {
    free(b->stops); b->stops = NULL; b->nstops = 0;
    int cap = 64; b->stops = xmalloc(sizeof(int) * cap);
#define ADD(i) do { if (b->nstops && b->stops[b->nstops - 1] == (i)) break; if (b->nstops == cap) { cap *= 2; b->stops = xrealloc(b->stops, sizeof(int) * cap); } b->stops[b->nstops++] = (i); } while (0)
    if (vis_n(b) <= 0) return;
    ADD(b->vis0);
    const BpStops *cs = &b->cs;
    int prev_op = -2, prev_tool = -2; double prev[3] = { NAN, NAN, NAN }; unsigned counter = 0;
    for (int i = b->vis0; i < b->vis1; i++) {
        const NcMotion *m = &b->m[i];
        if (m->op_index != prev_op) { if (cs->at_ops && prev_op != -2) ADD(i); prev_op = m->op_index; }          /* FUN_66d8b1f0 */
        if (m->kind == NCM_TOOL) {                                                                               /* FUN_66d8b400 */
            if (cs->at_toolchange && (prev_tool == -2 || m->tool != prev_tool) && prev_tool != -2) ADD(i);
            prev_tool = m->tool; continue;
        }
        const double *p = m->b; int stop = 0, skip = 0;                                                          /* FUN_66d8b260 */
        counter++;
        if (cs->at_step && cs->step && counter % cs->step == 0) stop = 1;
        else {
            const int chg[3] = { cs->chg_x, cs->chg_y, cs->chg_z }, at[3] = { cs->at_x, cs->at_y, cs->at_z };
            const double val[3] = { cs->x, cs->y, cs->z };
            for (int k = 0; k < 3; k++) {
                int changed = !isfinite(prev[k]) || fabs(p[k] - prev[k]) > 1e-5;
                if (changed && chg[k]) stop = 1;
                if (!changed && at[k]) skip = 1;                    /* value unchanged: no value stop at this point */
            }
            if (!stop && !skip) for (int k = 0; k < 3; k++) if (at[k] && fabs(p[k] - val[k]) < 1e-5) stop = 1;
        }
        if (stop) ADD(i);
        memcpy(prev, p, sizeof prev);
    }
#undef ADD
}

/* Info tab accumulation (section 6): over the visible motions. Rapid time needs the machine definition's axis
   feedrates (RapidMoveTime) - NOT DECODED here, so it is not computed. */
void bp_info(const Backplot *b, BpInfo *inf) {
    memset(inf, 0, sizeof *inf);
    for (int k = 0; k < 3; k++) { inf->lo[k] = 1e300; inf->hi[k] = -1e300; }
    inf->fmin = 1e300; inf->fmax = -1e300;
    for (int i = b->vis0; i < b->vis1; i++) {
        const NcMotion *m = &b->m[i];
        if (m->kind == NCM_TOOL) continue;
        for (int k = 0; k < 3; k++) { double a = m->a[k], c = m->b[k];
            inf->lo[k] = fmin(inf->lo[k], fmin(a, c)); inf->hi[k] = fmax(inf->hi[k], fmax(a, c)); }
        if (m->kind == NCM_RAPID) { inf->rapid_len += m->len; continue; }
        inf->feed_len += m->len;
        double F = fabs(m->feed);
        if (F > 0) { inf->feed_time_s += m->len / F * 60.0; inf->fmin = fmin(inf->fmin, F); inf->fmax = fmax(inf->fmax, F); inf->have_feed = 1; }
    }
}

/* Rapid / tool change time (section 6 FUN_67da5c00 / FUN_67dbb350, RapidMoveTime 13.6). Needs the machine definition's
   axis rapid rates; their place in the part's machine record is NOT DECODED, so MC->known is 0 and nothing is added. */
void bp_info_rapid(const Backplot *b, const BpMachine *mc, BpInfo *inf) {
    inf->rapid_time_s = 0; inf->have_rapid_time = 0;
    if (!mc || !mc->known) return;
    for (int i = b->vis0; i < b->vis1; i++) {
        const NcMotion *m = &b->m[i];
        if (m->kind == NCM_TOOL) { inf->rapid_time_s += mc->avg_tool_change / 60.0; continue; }   /* every tool change entity, x C4 = 1.0 */
        if (m->kind != NCM_RAPID) continue;
        double d[3] = { m->b[0] - m->a[0], m->b[1] - m->a[1], m->b[2] - m->a[2] };
        inf->rapid_time_s += bp_rapid_move_time(d, mc->feed, mc->ctrl0, mc->tol);                   /* x C3 = 1.0 */
    }
    inf->have_rapid_time = 1;
}

/* RapidMoveTime (13.6 step 3): first short of nc_path_linear_ctrl == 0 -> the axes move independently (the slowest
   axis decides); otherwise a vector move at the smallest non-zero axis rate. x 60 = seconds (3rd argument 1). */
double bp_rapid_move_time(const double delta[3], const double feed[3], int ctrl0, double tol) {
    double len = sqrt(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    if (len <= tol) return 0;
    double t = 0;
    if (ctrl0 == 0) {
        for (int k = 0; k < 3; k++) if (feed[k] > 0) { double tk = fabs(delta[k]) / feed[k]; if (tk > t) t = tk; }
    } else {
        double mn = 1e30;
        for (int k = 0; k < 3; k++) if (fabs(feed[k]) >= tol && feed[k] < mn) mn = feed[k];
        t = len / mn;
    }
    return t * 60.0;
}

/* FUN_1801b7980: config key -> mccore global (12.1 / 13.1). VAL = the text after "?" (y / n, integer or real). */
int bp_opt_cfg(BpOptions *o, int key, const char *val) {
    while (*val == ' ' || *val == '\t') val++;
    int yn = *val == 'y' || *val == 'Y', iv = atoi(val); double dv = atof(val);
    switch (key) {
    case 1000: if (dv > 0) o->tol = dv; return 1;      /* System tolerance (mccore tol) */
    case 19000: o->tool_appearance = iv; return 1;    case 19001: o->tool_material_on = yn; return 1;
    case 19002: o->tool_color = iv; return 1;         case 19003: o->tool_material = iv; return 1;
    case 19004: o->tool_as_defined = yn; return 1;    case 19005: o->spin_tool = yn; return 1;
    case 19006: o->holder_material_on = yn; return 1; case 19007: o->holder_color = iv; return 1;
    case 19008: o->holder_material = iv; return 1;    case 19010: o->color_loop_on_tc = yn; return 1;
    case 19011: o->verify_use_motion_color = yn; return 1;
    case 19013: o->rapid_color = iv; return 1;        case 19014: o->linear_color = iv; return 1;
    case 19015: o->cwarc_color = iv; return 1;        case 19016: o->ccwarc_color = iv; return 1;
    case 19017: o->verify_color = iv; return 1;       case 19018: o->vec_color = iv; return 1;
    case 19019: o->vec_on = yn; return 1;             case 19020: o->vec_check1 = yn; return 1;
    case 19021: o->vec_check2 = yn; return 1;         case 19022: o->vec_check3 = yn; return 1;
    case 19023: o->vec_length_cb = yn; return 1;      case 19024: o->vec_length = dv; return 1;
    case 19025: o->fast_steps = iv; return 1;         case 19026: o->cleanup_op = yn; return 1;
    case 19027: o->cleanup_tl = yn; return 1;         case 19028: o->fit_toolpath = yn; return 1;
    case 19029: o->sim_cutter_comp = yn; return 1;    case 19030: o->home_mill = yn; return 1;
    case 19031: o->simul_caxis = yn; return 1;        case 19032: o->simul_rot = yn; return 1;
    case 19033: o->toggles = strtoul(val, NULL, 10); return 1;
    case 19034: o->interp = yn; return 1;             case 19035: o->interp_dist = dv; return 1;
    case 19036: o->speed = iv; return 1;              case 19037: o->home_lathe = yn; return 1;
    case 19038: o->caxis_top = yn; return 1;          case 19039: o->opacity_active = dv; return 1;
    case 19040: o->opacity_inactive = dv; return 1;
    }
    return 0;
}
