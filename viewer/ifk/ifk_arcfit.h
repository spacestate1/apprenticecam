/* ifk_arcfit.h - port of mcmachinedef.dll's CSpinBdryArcFitter (Mastercam 2017) on the path Wireframe > Turn Profile
 * uses: the constructor 0x1800e06f0 (tp_ent **, short, l_3d &, double, bool) that mcmsurf FUN_18020de20 calls on the
 * line tp_ents FUN_18020c830 builds (formats/interfacial.md 10.5 / 14). C99, no dependencies. */
#ifndef IFK_ARCFIT_H
#define IFK_ARCFIT_H
#include <stdint.h>

#define IFK_TPENT_SIZE 0x78

/* A plane as the fitter reads it from Mastercam.dll (MastercamPlanes / Plane / PlaneBase). */
typedef struct {
    short id;            /* PlaneBase::GetShortID */
    short view;          /* PlaneBase::GetShortViewNo(true): the view number world_to_view / view_to_world use */
    double x[3];         /* Matrix33::GetX of PlaneBase::GetMatrix33 */
    double origin[3];    /* Plane::GetOriginInWorldCoordinates */
} IfkAfPlane;

typedef struct {
    double systol;       /* mccore ?tol@@3NA (the system tolerance; also l_ints / proj / ... inside mccore and systypes) */
    double disttol;      /* mcmachinedef g_distTol (180369040) = |mccore tol| when mcmachinedef.dll is loaded (initialiser
                            1800030a0); 5e-5 is mccore's static value of tol (data 1812b5b58) [inferred: Mastercam loads the
                            DLL before the configuration sets tol] */
    double min_arc_length; /* mccore ?min_arc_length@@3NA (BreakBdryArcQuadrants, FUN_18003db80) */
    IfkAfPlane wcs;      /* MastercamPlanes::GetActivePlanePtr(3) / GetActivePlaneID(3) */
    IfkAfPlane cplane;   /* MastercamPlanes::GetActivePlanePtr(0) / GetActivePlaneID(0) / GetActiveViewNo(0) */
    const IfkAfPlane *planes;   /* MastercamPlanes::GetPlanes, in that order (may be NULL when nplanes = 0) */
    int nplanes;
} IfkAfEnv;

typedef struct {
    int ok;              /* fitter +0x191: DefineNamedView() && (EvalIFBdry(true) || EvalIFBdry(false)) */
    short count;         /* fitter +0xa8: entries in the tp_ent array after the run (the input count when !ok) */
    short prof_id;       /* fitter +0xd8: the id of the profile plane (ProfileView = GetPlanePtrByID(prof_id)) */
    short prof_view;     /* that plane's view number (mcmsurf world_to_views the axis with it) */
    int set_active_cplane; /* DefineNamedView called MastercamPlanes::SetActivePlane(0, ProfileView): the plane came from
                              the GetPlanes loop, not from the active WCS / C-plane */
    int nassert;         /* _MCAssert calls the DLL would make (it continues after them) */
    int reallocated;     /* ToIFBdry freed the array and allocated a new one of `count` entries (zeroed) */
    int new_plane;       /* no plane of the part carries the axis: the DLL looks up (SearchPlanePtr) or adds
                            (MastercamPlanes::Add(plane, false, true)) the plane below and makes it the active C-plane;
                            prof_id / prof_view are then unknown (0 / -1: Mastercam.dll's, NOT DECODED), the fit itself
                            does not depend on them */
    double new_plane_m[9];   /* its Matrix33 rows: X = the unit axis, Y, Z */
    double new_plane_o[3];   /* its origin (the axis start) */
} IfkAfResult;

enum {
    IFK_AF_OK = 0,
    IFK_AF_NOT_DECODED = -1,  /* a path that is not decoded (see ifk_arcfit.c: SearchPlanePtr / Add of a new plane, views
                                 >= 2, a part with solids) - nothing is guessed */
    IFK_AF_NOMEM = -2,
    IFK_AF_LIMIT = -3          /* an array limit of the DLL that the port does not model (BrkBdryEntsInts' 10000 entries) */
};

/* Runs the constructor on *tp (n entries of 0x78 bytes, as FUN_18020c830 builds them; the array may have more slots, *alloc_n
 * is its size in entries). Like the DLL the array is rewritten in place (ToIFBdry) or, when the result has more entries than
 * n, replaced by a new zeroed array (malloc; the old one is freed with free()) of res->count entries; *alloc_n is updated.
 * axis = the l_3d (two world points), tol = the 4th argument (mcmsurf: dat+0x10 x 0.9; <= 0 means env->systol), flag = the
 * 5th (dat[0x4d] == 4). The part is assumed to have no solid entity (FindSpinBdrySolidTrueLinesAndArcs finds none). */
int ifk_arcfit(uint8_t **tp, int *alloc_n, short n, const double axis[6], double tol, int flag, const IfkAfEnv *env,
               IfkAfResult *res);

/* Test hook: the sin / cos / atan2 / acos the port calls (NULL = the C library's). The DLLs use msvcr120.dll's, which differ
 * from glibc's in the last ulp for some arguments; tools/peloader --arcfit-port runs the port with msvcr120's own functions. */
void ifk_arcfit_set_libm(double (*s)(double), double (*c)(double), double (*a2)(double, double), double (*ac)(double));

#endif
