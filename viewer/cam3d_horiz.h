/* cam3d_horiz.h - HST Horizontal Area (op 132, UI style 4 = record 0x16bc, kernel style 3): the flat levels and the clearing
 * passes on them. Spec: ../formats/horizontal_area.md (cited "ha N.N").
 *
 * Like the other HST 3D engines (cam3d_rough.c, cam3d_finish.c) the generator is compiled inside cam2d.c's unit (it uses MB / mv, the
 * Clipper wrapper, c3_mesh_op / c3_slice and area_mill_passes); CAM_HORIZAREA dispatches to gen_horizarea (cam2d.c).
 *
 * The flat detection is self-contained and exported for tests. */
#ifndef CAM3D_HORIZ_H
#define CAM3D_HORIZ_H

typedef struct { double zlo, zhi, area; } HorizBand;

/* McHmm's flat levels (CZInflexionsHorizontal, ha 2): the near-horizontal triangles of tri[9 * ntri] (x y z x 3, part units) whose Z
 * range overlaps [zmin, zmax] -> bands (z_lo, z_hi, 3D area) -> merged across gaps below `merge` -> kept when area >= amin.
 * grad = HorizontalInflexionGradient (0.0001), merge = HorizontalZEpsilonMerge, amin = HorizontalFlatAreaSize^2 (part units).
 * out[] gets the kept flats highest first (McHmm's order, ha 2.1.4); returns their count (<= max), or -1 when out of memory. */
int horiz_flats(const double *tri, int ntri, double zmin, double zmax, double grad, double merge, double amin, HorizBand *out, int max);

/* the McHmm parameter defaults of the plan (FUN_180313650, ha 2.3) for a tool / tolerance, part units (unit = part units per mm) */
typedef struct {
    double above, below, merge;      /* HorizontalZEpsilonAbove / Below / Merge */
    double flat_area;                /* HorizontalFlatAreaSize^2 */
    double grad;                     /* HorizontalInflexionGradient */
    double wall_out, tip_in;         /* HorizontalWallEdgeOffsetOut, HorizontalTipEdgeOffsetIn (signed) */
    int flat_tip;                    /* WeaveFlatTip */
} HorizDefaults;
void horiz_defaults(double R, double Rc, double stock_z, double tol, double unit, HorizDefaults *d);

#endif
