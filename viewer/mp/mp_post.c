/* mp_post - ours: the app's Mastercam-post chain (mp_post.h). The steps and the MpEnv fields are mp_cli's for a part input
 * (mp_cli.c: nci_ascii mp_nci_write_part into an .nci file, then mp_post_load / mp_run_new / mp_run_nci, a pending line
 * flushed at the end), so the app's program equals `mp_cli PART POST OUT --metric M --eol lf` (tools/tests/mpint_cmp.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mp_int.h"
#include "nci_ascii.h"
#include "mp_post.h"

/* mp_run_prm.c (MD / CD overrides, rd_md$ / rd_cd$, op parameters) is reached only through weak references (mp_int.h,
   mp_nciloop.c), which do not pull an object out of a static library (the app's mcamcore); without it the run silently uses
   the post's own values. This strong reference links it, as mp_cli's explicit object list does. */
int mp_run_override_md_cd(MpRun *r);
__attribute__((used)) static int (*const mp_post_link_prm)(MpRun *) = mp_run_override_md_cd;

int mp_post_part(const McamFile *f, const MpPostReq *q, FILE *out, int *nmsgs, char *last_msg, size_t nlast, char *err, size_t errn)
{
    if (nmsgs) *nmsgs = 0;
    if (last_msg && nlast) last_msg[0] = 0;
    char e2[512] = "";
    MpPost *p = mp_post_load(q->pst_path, e2, sizeof e2);
    if (!p) { snprintf(err, errn, "the post %s could not be read: %s", q->pst_path, e2); return 0; }
    for (int i = 0; i < mp_post_nwarnings(p); i++) fprintf(stderr, "pst: %s\n", mp_post_warning(p, i));   /* as mp_cli */
    /* the ASCII NCI (mp_cli: a file OUT.nci; here a temporary file) */
    FILE *nci = tmpfile();
    if (!nci) { mp_post_free(p); snprintf(err, errn, "cannot create a temporary NCI file"); return 0; }
    MpNciStats st; memset(&st, 0, sizeof st);
    if (!mp_nci_write_part_env(f, NULL, q->ops, q->nops, nci, &st, stderr, e2, sizeof e2)) {
        fclose(nci); mp_post_free(p); snprintf(err, errn, "NCI: %s", e2); return 0; }
    if (st.skipped) {                                   /* never a program with an operation left out or cut short */
        fclose(nci); mp_post_free(p);
        snprintf(err, errn, "operation %d could not be converted to NCI completely (see the nci_ascii messages) - the Mastercam post "
                 "posts the toolpaths Mastercam stored in the part", st.skipped_op + 1);
        return 0;
    }
    if (!st.ops) { fclose(nci); mp_post_free(p); snprintf(err, errn, "no toolpaths to post"); return 0; }
    rewind(nci);
    MpEnv env; memset(&env, 0, sizeof env);
    time_t now = time(NULL); struct tm *lt = localtime(&now);   /* the posting time (mp_cli: the same) */
    env.day = lt->tm_mday; env.mon = lt->tm_mon + 1; env.year = lt->tm_year + 1900; env.hour = lt->tm_hour; env.min = lt->tm_min; env.sec = lt->tm_sec;
    env.metric = q->metric; env.product = q->product; env.eol = q->eol;
    env.part_path = f->path; env.prm_ctx = (void *)f;
    env.nc_path = q->nc_path; env.pst_path = q->pst_path; env.nci_path = f->path;   /* mp_cli: nci_path = its input (the part) */
    MpRun *r = mp_run_new(p, &env, out, e2, sizeof e2);
    if (!r) { fclose(nci); mp_post_free(p); snprintf(err, errn, "%s", e2); return 0; }
    int ok = mp_run_nci(r, nci);
    if (r->linelen) mp_out_eob(r, 0);                   /* mp_cli (ours): flush a pending line */
    fclose(nci);
    if (mp_run_error(r)) {
        if (nmsgs) *nmsgs = r->nerrors;
        if (last_msg && nlast) snprintf(last_msg, nlast, "%s", mp_run_error(r));
    }
    if (!ok) snprintf(err, errn, "the post stopped: %s", mp_run_error(r) ? mp_run_error(r) : "run error");
    mp_run_free(r); mp_post_free(p);
    return ok;
}
