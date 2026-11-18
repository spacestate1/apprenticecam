/* ifk_silhouette_in.h - input stage of the Silhouette Boundary kernel (interfacial11.dll Silhouette::Run 18001f1c0):
 * the IO SurferToSilhouette slots begin FUN_180023600, AddNode FUN_180023710, AddTriangle FUN_180023920 and
 * end FUN_180023f80 up to (not including) PlaneCut::Run. See ifk_silhouette_in.c for the citations. */
#ifndef IFK_SILHOUETTE_IN_H
#define IFK_SILHOUETTE_IN_H

typedef struct {
    /* unique snapped nodes (AddNode FUN_180023710), in creation order; node_of_input maps input node -> unique */
    int nnode;
    double *node;          /* 3 x nnode */
    int *node_of_input;    /* nnode_in */

    /* kept facets (SilhouetteData +0xa0 list, prepend order = newest kept first), node ids after the nz<0 swap */
    int nkept;
    int *kept_tri;         /* 3 x nkept, unique-node indices */
    int *kept_src;         /* nkept, input triangle index */
    double *kept_angle;    /* nkept, radians, acos(nz/|n|) of the upward normal */
    int ndrop_vertical;    /* nz == 0 */
    int ndrop_angle;       /* rejected by the mode / min / max filter */
    double box[4];         /* xmin, ymin, xmax, ymax of the kept facets (SilhouetteData +0x20..+0x38) */

    /* silhouette edges in edge-tree order (start, end = the head triangle's traversal), unique-node indices */
    int nsil;
    int *sil_edge;         /* 2 x nsil */

    /* PlaneCut input: nodes in PlaneCut::AddInputNode call order, triangles in AddInputTriangle call order */
    int nwnode;
    double *wnode;         /* 3 x nwnode (x, y, z) */
    int nwtri;
    int *wtri;             /* 3 x nwtri: (start, end, mid) indices into wnode */
    int *wtri_id;          /* nwtri: the id argument (1, 2, ...) */

    int overflow;          /* TLS DAT_180268398: "IFSilhouette error 2: overflow." */
} IfkSilWalls;

/* node: 3 x nnode; tri: 3 x ntri, 0-based; angles in degrees (Run converts deg * pi / 180);
 * mode 1: keep min <= angle <= max, mode 0: keep angle < min or angle > max (blob byte 0xb -> SilhouetteData +0x70).
 * Returns 0, 1 when the overflow flag is set (outputs still filled), -1 on allocation failure. */
int ifk_silhouette_walls(const double *node, int nnode, const int *tri, int ntri,
                         double minang_deg, double maxang_deg, int mode, IfkSilWalls *out);
void ifk_silhouette_walls_free(IfkSilWalls *w);

#endif
