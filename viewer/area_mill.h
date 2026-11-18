/* area_mill - 2D High Speed "Area Mill" (op 134, style 0x1228 = 1): the decoded mcmill / mccore / mckernel side.
 *
 * Spec: ../formats/area_mill.md. Mastercam does NOT compute the Area Mill pattern in mcmill or mcce: mcmill writes the
 * region as layered boundaries ("areaclear2b" file), turns them into a triangle model ("tmpCoreMill*.raw") and calls
 * mccore run_mchutl_coreareamill -> mckernel TP2DHSTCoreAreaMill::Compute, which builds an NCG "Machining Passes"
 * macro and runs it in McHmm.dll (CNCGDCInterface::RunMacro). McHmm's Area Clearance pass positions are decoded
 * (formats/area_mill.md 11) and ported below (area_mill_passes, used by cam2d_area.c); Core Roughing and McHmm's
 * linker are NOT DECODED.
 *
 * What is ported here: the parameter plumbing from the op record to the NCG macro (plan choice + every parameter
 * name / value mckernel writes), and the arc tessellation mcmill applies to the region boundaries. */
#ifndef AREA_MILL_H
#define AREA_MILL_H
#include <stdint.h>
#include "offset.h"

typedef struct {
    const char *name;     /* NCG macro parameter name (mckernel string) */
    double value;         /* value as written to the macro (already x unit scale: mm) */
    char kind;            /* 'd' double, 'b' bool, 'i' int */
} AreaMillParam;

typedef struct {
    int mode;             /* hmmCoreAreaMillDat +0x84: 0 Core Roughing, 1 Area Clearance, 4 with trim boundaries,
                             5 Horizontal (island tops) */
    const char *passes_plan, *linking_plan;  /* NCG plan comments as mckernel FUN_18006bd90 writes them */
    int passes_id, linking_id, edit_to_boundaries;   /* plan type ids; mode 4: + "Edit Passes to Boundaries" 0x1416 */
    double tol;           /* CHST2DProcess +0x48 = record 0x1288 (default 0.001 in / 0.025 mm) = StepMachTolerance */
    double scale;         /* macro unit factor: 25.4 inch, 1.0 metric */
    int n;
    AreaMillParam p[48];
    int expansion_not_decoded;   /* mode 0: CoreRoughingExpansionX/Y come from layer boxes (FUN_1802a9c60): NOT DECODED */
} AreaMillMacro;

/* Build the macro parameters for one depth of an Area Mill op.
 *   rec, n      : the op record (record offsets, formats/mcmill_2d_engines.md 0.1)
 *   metric      : part units (MCSystemContext +8 = IsMetric)
 *   sys_tol     : MCSystemContext +0x10 = system tol (cam2d_ops.c uses 5e-5)
 *   horizontal  : 1 for the island-top pass (PackageSomething second vtable[1] call, in_stack_28 = 0), else 0
 *   step_dz     : in_stack_30 - in_stack_38 of CHST2DStandard vtable[1] (FUN_18029d3e0); the caller's values are
 *                 XMM registers not decoded (formats/area_mill.md 7) - pass 0 to get the record part only
 * Returns 1, or 0 if the record is too short / not op 134 style 1. */
int area_mill_macro(const uint8_t *rec, uint32_t n, int metric, double sys_tol, int horizontal, double step_dz,
                    AreaMillMacro *out);

/* value of a macro parameter by name (as written, x unit scale), or def when the macro does not write it */
double area_mill_param(const AreaMillMacro *m, const char *name, double def);

/* chords mcmill uses for one boundary arc (FUN_1802a1670 l.459503-459516): radius r, sweep (radians, > 0), tol */
int area_mill_arc_segments(double r, double sweep, double tol);

/* the arc sweep / direction rule FUN_18029caf0 (angles a0 -> a1, cw flag): returns the sweep (>= 0), *dir = +1 / -1 */
double area_mill_arc_sweep(int cw, double a0, double a1, double *dir);
#endif

/* ---- McHmm Area Clearance passes (formats/area_mill.md 11.3) ----
 * T0 = the tool region (machining region shrunk by tool radius + stock), non-zero CPaths.
 * Positions (decoded, McHmm voronoi\xoipositionoffsets.cpp FUN_180555960): from the boundary (t = 0), each next pass
 * is where the medial axis, followed for E = RadExtra (MaxOffset - MinOffset) from the point S = MinOffset beyond
 * the previous pass, has climbed to; the smallest climb over the axis branches of the region decides. A pass is the
 * offset of T0 by t (the iso-clearance loop). The medial axis is taken on a clearance grid of spacing h (our
 * approximation of McHmm's exact Voronoi diagram; standard semantics: distance to the boundary).
 * Output order: inner passes first (measured on buckle-v10 op 2, the McHmm linker FragOrderer is NOT DECODED). */
typedef struct { CPaths loops; double t; } AreaMillPass;     /* loops: outer (CCW) + its holes (CW) */
typedef struct { AreaMillPass *p; int n, cap; } AreaMillPasses;
int area_mill_passes(const CPaths *T0, double first_step, double step, double extra, double tau, double arc_tol, double unit,
                     AreaMillPasses *out);   /* unit = part units per mm (McHmm works in mm) */
void area_mill_passes_free(AreaMillPasses *ps);
/* McHmm "MeasureCurveDiameter" (FUN_18004cf60 / FUN_18004cb90): the largest distance between two points of a curve */
double area_mill_curve_diameter(const CPath *c);
