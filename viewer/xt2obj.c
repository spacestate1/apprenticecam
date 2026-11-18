/* xt2obj - mesh the Parasolid solids of a Mastercam part file and write them as OBJ (test tool for xt_mesh). */
#include <stdio.h>
#include <stdlib.h>
#include "mcam_load.h"
#include "xt_mesh.h"
extern long xt_dbg_arcs, xt_dbg_off;
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: xt2obj PART.mcx-9 SCHEMA.s_t [out.obj]\n"); return 1; }
    McamFile f; if (!mcam_load(&f, argv[1])) return 1;
    XtSchema *s = xt_schema_load(argv[2]); if (!s) { fprintf(stderr, "schema?\n"); return 1; }
    FILE *o = argc > 3 ? fopen(argv[3], "w") : NULL; long vbase = 1;
    for (int i = 0; i < f.nsolids; i++) {
        XtMesh m;
        int ok = xt_mesh(s, f.solids[i].xt, f.solids[i].xt_size, &m);
        printf("%s: %s nodes %d faces %d meshed %d triangles %d %s\n", f.solids[i].name, ok ? "ok" : "FAIL",
               m.nnodes, m.nfaces, m.nfaces_meshed, m.ntri, m.error);
        if (o && ok) {
            fprintf(o, "o %s\n", f.solids[i].name);
            for (int t = 0; t < m.ntri * 3; t++) fprintf(o, "v %g %g %g\n", m.tri[t * 6] * 1000, m.tri[t * 6 + 1] * 1000, m.tri[t * 6 + 2] * 1000);
            for (int t = 0; t < m.ntri; t++) fprintf(o, "f %ld %ld %ld\n", vbase + t * 3, vbase + t * 3 + 1, vbase + t * 3 + 2);
            vbase += m.ntri * 3;
        }
        xt_mesh_free(&m);
    }
    printf("arc endpoint check: %ld arcs, %ld with endpoints off the circle\n", xt_dbg_arcs, xt_dbg_off);
    if (o) fclose(o);
    return 0;
}
