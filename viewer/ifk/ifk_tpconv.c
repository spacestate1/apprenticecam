/* ifk_tpconv.c - the two conversions around CSpinBdryArcFitter in mcmsurf FUN_18020de20 (Turn Profile, formats/interfacial.md
 * 14.5): FUN_18020c830 (filtered HBE records -> tp_ent lines, ConvertHmmFiltrBndyArrayToTpEnts) and FUN_18020cb90 (tp_ents ->
 * HBE records with the profile's mirror passes, ConvertTpEntsToHmmFiltrBndyArray). tp_ents are kept as their 0x78-byte images:
 * +0 int type (2 line, 4 arc), +4 byte colour, +6 byte 3, +0x10 a l_3d (start, end) or an a_3d (start +0x10, end +0x28,
 * centre +0x40, r +0x58, start angle +0x60, sweep +0x68, view +0x70). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "ifk_tpconv.h"

static double rd(const uint8_t *p, int o) { double v; memcpy(&v, p + o, 8); return v; }
static void wd(uint8_t *p, int o, double v) { memcpy(p + o, &v, 8); }

uint8_t *ifk_hbe_to_tpents(const IfkHbe68 *rec, int n, double tol, uint8_t colour, short *count)
{
    *count = 0;
    if (n < 1) return NULL;
    uint8_t *tp = calloc((size_t)(short)((short)n + 1), IFK_TPENT);   /* AllocBdryEntMemNormal(n + 1), memset 0 */
    if (!tp) return NULL;
    double q[3]; memcpy(q, rec[0].p0, 24);                             /* the running start: record 0's start */
    for (int i = 0; i < n; i++) {
        uint8_t *t = tp + IFK_TPENT * (size_t)*count;
        t[4] = colour; t[6] = 3;                                       /* lathe_cfg_stock_color, 3: written before the tests */
        if (rec[i].type != 1) continue;                                /* debug_str "... Arcs not supported at this time!" */
        if (0 < i) {
            const double *a = rec[i - 1].p1, *b = rec[i].p0;
            double dy = b[1] - a[1], dx = b[0] - a[0], dz = b[2] - a[2];
            double d = sqrt(dy * dy + dx * dx + dz * dz);
            if (tol < d) memcpy(q, rec[i].p0, 24);                     /* a gap: restart at this record's start */
        }
        const double *e = rec[i].p1;
        double dy = e[1] - q[1], dx = e[0] - q[0], dz = e[2] - q[2];
        double d = sqrt(dy * dy + dx * dx + dz * dz);
        if (d < tol || d == tol) continue;                             /* too short: no tp_ent, the running start kept */
        int32_t two = 2; memcpy(t, &two, 4);
        for (int k = 0; k < 3; k++) { wd(t, 0x10 + 8 * k, q[k]); wd(t, 0x28 + 8 * k, e[k]); }
        memcpy(q, e, 24);
        (*count)++;
    }
    return tp;
}

int ifk_tpents_to_hbe(const uint8_t *tp, short count, int spin, int profile, int old_count, IfkHbe68 *out, int *nout)
{
    if (!tp || count < 1) return 0;
    if (old_count < count) return 0;                                   /* +0x164 must hold at least count */
    int first = 0, last = 1;
    if (spin) {
        if (profile == 0) { first = 0; last = 1; }
        else if (profile == 1) { first = 1; last = 2; }
        else if (profile == 2) { first = 0; last = 2; }
        else return -1;                                                /* _MCAssert(0x172a), then one pass as is: not modelled */
    }
    int k = 0;
    for (int pass = first; pass < last; pass++) {
        for (int i = 0; i < count; i++) {
            const uint8_t *t = tp + IFK_TPENT * (size_t)i;
            int32_t type; memcpy(&type, t, 4);
            double s[3], e[3], c[3], a60 = rd(t, 0x60), a68 = rd(t, 0x68);
            for (int j = 0; j < 3; j++) { s[j] = rd(t, 0x10 + 8 * j); e[j] = rd(t, 0x28 + 8 * j); c[j] = rd(t, 0x40 + 8 * j); }
            if (pass == 1) {                                           /* mirrored in y (FUN_1800062d0(p, 1) = &p.y) */
                if (type == 2) { s[1] *= -1.0; e[1] *= -1.0; }
                else { a60 *= -1.0; a68 *= -1.0; s[1] *= -1.0; e[1] *= -1.0; c[1] *= -1.0; }
            }
            IfkHbe68 *r = &out[k++];
            /* the record bytes the original leaves alone keep the buffer's old content: here zeroed by the caller */
            r->first = i == 0;
            memcpy(r->p0, s, 24); memcpy(r->p1, e, 24);
            if (type == 2) r->type = 1;
            else {
                r->type = 2; memcpy(r->c, c, 24);
                r->n[0] = 0.0; r->n[1] = 0.0; r->n[2] = a68 <= 0.0 ? -1.0 : 1.0;
            }
        }
    }
    *nout = k;
    return 1;
}
