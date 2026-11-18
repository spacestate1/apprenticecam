/* mp_post - ours: post a loaded part's stored operations through a Mastercam .pst with the MP engine (viewer/mp), the
 * same chain as mp_cli PART POST OUT (nci_ascii -> mp_post_load -> mp_run_new -> mp_run_nci), for the app's Post when the
 * machine group's "Post with Mastercam post (.pst)" setting is on (core_ui cui_ops.c post_ops; formats/mp_engine.md
 * "Integration (app)"). It never writes a partial program: an operation the NCI conversion could not write completely is
 * refused (MpNciStats.skipped). */
#ifndef MP_POST_H
#define MP_POST_H
#include <stdio.h>
#include <stddef.h>
#include "../mcam_load.h"

typedef struct {
    const char *pst_path;     /* the .pst on this host (already resolved: paths.h host_path) */
    const char *nc_path;      /* the NC file name the post sees (spathnc$ / snamenc$ / sextnc$) */
    const char *eol;          /* MpEnv.eol: NULL = mcpost's "\r\n", the app passes "\n" (user decision P3) */
    int metric;               /* MpEnv.metric (initial met_tool$ / IsMetric) */
    int product;              /* MpEnv.product: 0 mill, 1 router (lathe / wire are not posted here: user decision P4) */
    const int *ops; int nops; /* 0-based F.ops indexes (written in F.ops order, nci_ascii) */
} MpPostReq;

/* posts into OUT. Returns 1 ok (*nmsgs = the run messages the post printed, see stderr; *last_msg the last one, may be
   NULL), 0 refused / failed (err). */
int mp_post_part(const McamFile *f, const MpPostReq *q, FILE *out, int *nmsgs, char *last_msg, size_t nlast, char *err, size_t errn);

#endif
