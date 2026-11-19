/* occt_solid_ent.h - a chain's line / arc entity as the Extrude kernel's exact segment (occt_solid.h OcsSeg). The circle follows
 * ps_arc_to_ps_arc (mcgeomsld 0x18003e060, formats/extrude.md 4.9): centre = the arc centre, axis = the view's Z row (world +Z for an XY
 * arc), negated for a negative sweep, ref_direction = the view's X row; the entity's interval [t0, t1] on that circle (a negative sweep
 * mirrored: t0 = 2 pi - a0). A point of a view arc at angle t is c + r (cos t vm[0..2] + sin t vm[3..5]) (mcam_load.h Ent). */
#ifndef OCCT_SOLID_ENT_H
#define OCCT_SOLID_ENT_H
#include <math.h>
#include "mcam_load.h"
#include "occt_solid.h"

static inline int ocs_seg_of_ent(const Ent *e, int rev, int ent, OcsSeg *s) {
    s->rev = rev ? 1 : 0; s->ent = ent;
    s->p0[0] = e->a.x; s->p0[1] = e->a.y; s->p0[2] = e->a.z; s->p1[0] = e->b.x; s->p1[1] = e->b.y; s->p1[2] = e->b.z;
    if (e->type == E_LINE) { s->type = 1; return 1; }
    if (e->type != E_ARC || !(e->r > 0) || e->sw == 0) return 0;
    s->type = 2; s->r = e->r; s->c[0] = e->c.x; s->c[1] = e->c.y; s->c[2] = e->c.z;
    double X[3] = { 1, 0, 0 }, Y[3] = { 0, 1, 0 }, Z[3] = { 0, 0, 1 };
    if (e->vw >= 2) for (int k = 0; k < 3; k++) { X[k] = e->vm[k]; Y[k] = e->vm[3 + k]; Z[k] = e->vm[6 + k]; }
    (void)Y;
    double sw = fabs(e->sw) > 2 * M_PI ? 2 * M_PI : fabs(e->sw);
    double t0 = e->sw > 0 ? e->a0 : -e->a0;
    t0 = fmod(t0, 2 * M_PI); if (t0 < 0) t0 += 2 * M_PI;       /* restrict_angle(0, 2 pi) (FUN_180043850's rule) */
    for (int k = 0; k < 3; k++) { s->ax[k] = e->sw > 0 ? Z[k] : -Z[k]; s->xd[k] = X[k]; }
    s->t0 = t0; s->t1 = t0 + sw;
    return 1;
}
#endif
