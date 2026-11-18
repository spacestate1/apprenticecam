/* cui_doc.c - the document: the part + the .mcamui project sidecar (feats, camops, RP lines), open / save / close, the
   dirty flag, the recent-files list (core_ui.h). Moved from viewer/mcamui.c; comments and citations moved with it. */
#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "paths.h"
#include "toollib.h"
#include "nci_reverse.h"
#include "cam_postvals.h"
#include "core_ui.h"
#include "cad_import.h"
#include "mcam_saveops.h"

#define CP(dst, src) snprintf(dst, sizeof dst, "%s", src)

CuiHooks cui_hooks;
char cam_msg[300];
/* UpdateStatusBarText (mastercam.dll 0x180329cc0, decomp/surfedit2/mc_statustext.c): the main frame's CBCGPStatusBar (frame +0x9bb8,
   FUN_180263090) pane 0 gets the text (SetPaneText(0, text, TRUE), vtable +0x678) only when it differs from the pane's text
   (GetPaneText + Compare). The text stays until the next call. Ours: cui_status (the front end shows it in its status bar's first
   pane; cui_status_gen counts the changes). Where pane 0 sits among the ribbon status bar's X / Y / Z panes is NOT DECODED. */
char cui_status[300]; int cui_status_gen;
void cui_status_text(const char *t) {
    if (!t) t = "";
    if (strcmp(cui_status, t)) { snprintf(cui_status, sizeof cui_status, "%s", t); cui_status_gen++; }
}
/* message boxes go to the front end (mcamui message(): the same 700-byte text) */
void cui_message(const char *title, const char *fmt, ...) {
    char t[700]; va_list ap; va_start(ap, fmt); vsnprintf(t, sizeof t, fmt, ap); va_end(ap);
    if (cui_hooks.message) cui_hooks.message(title, t); else fprintf(stderr, "%s: %s\n", title, t);
}

#define MAX_CAMOPS 1024                                      /* = the Operations Manager selection size (op_msel) */
CamOp *camops[MAX_CAMOPS]; int ncamops;
char doc_path[1100];          /* project file, "" = not saved yet */
int doc_dirty;                /* operations / tools changed since the last save */
char machine[16] = "Mill";
char recent_files[12][1100]; int nrecent_files;
char cad_doc_name[1100];      /* an imported CAD file: geo_filename = its path with the .mcam extension (DoFileOpen: ensureMCX); "" otherwise */
int file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }

int ends_with_ci(const char *s, const char *e) { size_t a = strlen(s), b = strlen(e); return a >= b && !strcasecmp(s + a - b, e); }
int is_project_file(const char *p) { return ends_with_ci(p, ".mcamui"); }
int is_part_file(const char *p) {           /* .mcam, .mcx, .mcx-5 .. .mcx-9 (never written) */
    const char *d = strrchr(p, '.'); if (!d) return 0;
    return !strcasecmp(d, ".mcam") || !strncasecmp(d, ".mcx", 4) || !strcasecmp(d, ".emcam");
}

void config_dir_make(char *out, size_t n) { snprintf(out, n, "%s", config_dir()); make_dirs(out); }   /* platform.h */

void recent_load(void) {
    char p[1200], dir[1100]; config_dir_make(dir, sizeof dir); path_join(p, sizeof p, dir, "recent.txt");
    FILE *f = fopen(p, "r"); nrecent_files = 0;
    if (!f) return;
    char line[1100];
    while (nrecent_files < 12 && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0]) CP(recent_files[nrecent_files++], line);
    }
    fclose(f);
}

void recent_add(const char *path) {
    if (getenv("MCAMUI_SCRIPT")) return;                 /* scripted test runs leave the user's recent list alone */
    char abs[1100]; full_path(path, abs, sizeof abs);
    int i = 0; while (i < nrecent_files && strcmp(recent_files[i], abs)) i++;
    if (i == nrecent_files) i = nrecent_files < 12 ? nrecent_files++ : 11;
    memmove(recent_files + 1, recent_files, i * sizeof recent_files[0]);
    CP(recent_files[0], abs);
    char p[1200], dir[1100]; config_dir_make(dir, sizeof dir); path_join(p, sizeof p, dir, "recent.txt");
    FILE *f = fopen(p, "w"); if (!f) return;
    for (int k = 0; k < nrecent_files; k++) fprintf(f, "%s\n", recent_files[k]);
    fclose(f);
}

void doc_clear_ops(void) {
    cad_doc_name[0] = 0;
    undo_reset(); solids_clear(); planes_clear_user();   /* planes made here (cui_planes.c) */
    dirty_clear_doc();                                     /* stored ops' changed parameters, dialog 977 (cui_dirty.c) */
    xf_reset_doc();                                        /* the Transform group / result marks (cui_xform.c) */
    pv_clear();                                            /* Q20: no preview survives the document */
    if (cui_hooks.clear_ops) cui_hooks.clear_ops();        /* mcamui: vf_stop(); cam_mode = 0; show_params = 0; tool_choice = 0; cam_clear_selection() */
    /* L-15: the new Lathe Face sheet is modal in Mastercam (edit_lathe_operation 0x1801c7480: a CLTpSheet on its stack run by MFC Ordinal_3916 = DoModal, its result used) - no part closes
       under it; here a script / the app can close the part with it up: it is cancelled as its Cancel does (lface_cancel =
       DeleteListAndDB), so nothing of the closed part stays behind it */
    if (show_lface) lface_cancel();
    for (int i = 0; i < ncamops; i++) cam_op_free(camops[i]);
    ncamops = 0;
}

/* a surface record in the project (ours): "<TAG> a b nbytes" then "SH <hex>" lines of up to 400 bytes; the references to entities
   made here (mcam_surf_ref_offsets) are written as 0x7d00000000000000 + their entity index (their session eptr changes per session) */
static void surf_write(FILE *f, const char *tag, int e, int a, int b) {
    const Surf *sf = &F.surfs[F.ents[e].srf];
    uint8_t *r = malloc(sf->n); memcpy(r, sf->rec, sf->n);
    uint32_t offs[8]; int no = mcam_surf_ref_offsets(r, sf->n, offs);
    for (int k = 0; k < no; k++) { int64_t v; memcpy(&v, r + offs[k], 8);
        for (int q = 0; q < F.nents; q++) if (F.ents[q].created && F.ents[q].septr == v) { v = 0x7d00000000000000LL + q; memcpy(r + offs[k], &v, 8); break; } }
    fprintf(f, "%s %d %d %u\n", tag, a, b, sf->n);
    for (uint32_t o = 0; o < sf->n; o += 400) {
        fputs("SH ", f); for (uint32_t k = o; k < sf->n && k < o + 400; k++) fprintf(f, "%02x", r[k]); fputc('\n', f);
    }
    free(r);
}
static uint8_t *cv_buf; static uint32_t cv_n, cv_at; static int cv_lv, cv_co;   /* the CURVE being read (its SH lines) */
static size_t bh_at; static int bh_i = -1;                                       /* the OCCSOLID being read (its BH lines) */
static uint8_t *sr_buf; static uint32_t sr_n, sr_at; static int sr_a, sr_b, sr_edit;   /* the surface record being read */
static uint8_t *mh_buf; static size_t mh_n, mh_at; static int mh_lv, mh_co, mh_np, mh_mixed;   /* the mesh being read (MESH / MH) */
static void surf_read_done(void) {
    uint32_t offs[8]; int no = mcam_surf_ref_offsets(sr_buf, sr_n, offs);
    for (int k = 0; k < no; k++) { int64_t v; memcpy(&v, sr_buf + offs[k], 8);
        if (v >= 0x7d00000000000000LL && v < 0x7e00000000000000LL) { int q = cam_load_ent_remap((int)(v - 0x7d00000000000000LL)); if (q >= 0 && q < F.nents) { v = F.ents[q].septr; memcpy(sr_buf + offs[k], &v, 8); } } }
    if (sr_edit) {
        for (int i = 0; i < F.nsurfs; i++) if (F.surfs[i].idn == sr_a && F.surfs[i].n == sr_n) { memcpy(F.surfs[i].rec, sr_buf, sr_n); F.surfs[i].edited = 1; break; }
    } else {
        int k = mcam_new_surf(&F, sr_buf, sr_n, sr_a < 0 ? 1 : sr_a, sr_b);
        if (sr_a < 0) mcam_delete_ent(&F, k);
    }
    free(sr_buf); sr_buf = NULL; sr_n = sr_at = 0;
}
/* the project's format (ours: the PROJECT-FORMAT line): 2 = entity indices with the part's notes as entities after its surfaces (E_NOTE,
   mcam_load.c; 2026-11-05); 1 / no line = written before, when the entities made here came right after the part's surfaces - read with
   the indices from there on moved up by the part's note count (cam2d.h cam_load_remap_*) */
#define DOC_FORMAT 2
/* write the project atomically: <path>.tmp, then the old file becomes <path>.bak and the new one replaces it */
int doc_write(const char *path) {
    if (is_part_file(path)) { cui_message("Save", "Refusing to write %s: part files are never overwritten. Save the project as a .mcamui file.", path); return 0; }
    char tmp[1200], bak[1200]; snprintf(tmp, sizeof tmp, "%s.tmp", path); snprintf(bak, sizeof bak, "%s.bak", path);
    FILE *f = fopen(tmp, "w");
    if (!f) { cui_message("Save failed", "Cannot write %s: %s", tmp, strerror(errno)); return 0; }
    fprintf(f, "MCAMUI-PROJECT 1\n# CAM workbench project: part reference, tools and operations (toolpaths are regenerated on open)\n");
    fprintf(f, "PROJECT-FORMAT %d\n", DOC_FORMAT);         /* ours: the entity numbering the indices below use (doc_read) */
    if (loaded && F.path[0]) { char abs[1100]; full_path(F.path, abs, sizeof abs); fprintf(f, "PART %s\n", abs); }
    else fprintf(f, "NEWPART\n");
    /* the system units when they are not the ones opening the part gives (the header's; a new part: the program's metric start):
       ours, the project line of a units change (cui_units_set; formats/units.md) */
    if (loaded && mcam_is_metric(&F) != (part_file_units == 0 || part_file_units == 1 ? part_file_units : 1)) fprintf(f, "UNITS %d\n", mcam_is_metric(&F));
    for (int i = 0; loaded && i < F.nents; i++) {          /* geometry drawn here (in order) and part geometry deleted here */
        const Ent *en = &F.ents[i];
        if (en->created && en->cls == 0x100000 && en->msh >= 0 && en->msh < F.nmeshes) {   /* a mesh made here (STL import; ours: MESH level colour
                                                                      count mixed, then MH hex lines: the 0x130-byte record, per facet 12 floats + the byte) */
            const McMesh *m = &F.meshes[en->msh]; size_t n = 0x130 + (size_t)m->np * 49; uint8_t *b = malloc(n);
            memcpy(b, m->rec, 0x130);
            for (int k = 0; k < m->np; k++) { uint8_t *q = b + 0x130 + (size_t)k * 49; memcpy(q, m->p[k].n, 12); memcpy(q + 12, m->p[k].v, 36); q[48] = m->p[k].attr; }
            fprintf(f, "MESH %d %d %d %d\n", en->type ? en->level : -1, en->color, m->np, m->mixed);
            for (size_t o = 0; o < n; o += 400) { fputs("MH ", f); for (size_t k = o; k < n && k < o + 400; k++) fprintf(f, "%02x", b[k]); fputc('\n', f); }
            free(b);
            if (en->type && (en->ln_style != 1 || en->width != 1 || en->blanked)) fprintf(f, "GEOATTR -1 %d %.17g %d\n", en->ln_style, en->width, en->blanked);
            continue;
        }
        if (en->created && en->srf >= 0 && en->srf < F.nsurfs && en->cls == 0x100) {   /* a surface made here (ours: SURF + SH hex lines) */
            surf_write(f, "SURF", i, en->type ? en->level : -1, en->color);
            continue;
        }
        if (en->created && en->type == E_NOTE) { dn_write_ent(f, i); continue; }   /* a note made here (cui_draft_note.c: NOTE line) */
        if (en->created && en->type == E_SPLINE && en->crv >= 0 && en->crv < F.ncrvs) {   /* a spline made here (ours: CURVE + SH hex lines; Transform copies and imported splines) */
            const CurveRec *c = &F.crvs[en->crv];
            fprintf(f, "CURVE %d %d %u\n", en->level, en->color, c->n);
            for (uint32_t o = 0; o < c->n; o += 400) { fputs("SH ", f); for (uint32_t k = o; k < c->n && k < o + 400; k++) fprintf(f, "%02x", c->rec[k]); fputc('\n', f); }
            if (en->ln_style != 1 || en->width != 1 || en->blanked) fprintf(f, "GEOATTR -1 %d %.17g %d\n", en->ln_style, en->width, en->blanked);
            continue;
        }
        if (en->created) {   /* a view arc (Ent.vw) appends its view number (ours: older lines have none = XY) */
            fprintf(f, "GEO %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %d %d", en->type,
                    en->a.x, en->a.y, en->a.z, en->b.x, en->b.y, en->b.z, en->c.x, en->c.y, en->c.z, en->r, en->a0, en->sw, en->level, en->color);
            if (en->type == E_ARC && en->vw) {               /* the view number + its matrix rows (any view, also one not reserved) */
                fprintf(f, " %d", en->vw);
                for (int k = 0; k < 9; k++) fprintf(f, " %.17g", en->vm[k]);
            }
            fputc('\n', f);
            if (en->ln_style != 1 || en->width != 1 || en->blanked) fprintf(f, "GEOATTR -1 %d %.17g %d\n", en->ln_style, en->width, en->blanked);   /* ours: the line style / width / blank of an imported entity */
        }
        else if (!en->type && en->idn) fprintf(f, "DEL %d\n", en->idn);
    }
    for (int i = 0; loaded && i < F.nsurfs; i++) {          /* part surfaces whose record was edited here (ours: SURFEDIT + SH lines) */
        int e = F.surfs[i].ent;
        if (F.surfs[i].edited && e >= 0 && e < F.nents && !F.ents[e].created && F.ents[e].type) surf_write(f, "SURFEDIT", e, F.surfs[i].idn, 0);
    }
    dr_write(f);                                             /* drafting entities made here (DIM lines, cui_draft.c) */
    feats_write(f);                                          /* solids made here (Extrude features) */
    for (int i = 0; i < nuplanes; i++) {                     /* planes made here (ours: UPLANE id m[9] org[3] name); the part's own are restored from the part */
        const UPlane *u = &uplanes[i]; if (u->part) continue; fprintf(f, "UPLANE %d", u->id);
        for (int k = 0; k < 9; k++) fprintf(f, " %.17g", u->m[k]);
        for (int k = 0; k < 3; k++) fprintf(f, " %.17g", u->org[k]);
        fprintf(f, " %s\n", u->name);
    }
    if (planes_project_active_differs())
        fprintf(f, "ACTPLANE %d %d %d %d %d %d %d\n", plane_active[0], plane_active[1], plane_active[2], plane_active[3],
                plane_active_org[0], plane_active_org[1], plane_active_org[3]);   /* + the active origin ids of Cplane / Tplane / WCS (SystemPlanes +0x120; 2026-11-12) */
    if (F.stock_dirty)                                       /* the Stock Setup changed here (cui_stock.c; ours: the project line) */
        fprintf(f, "STOCK %d %d %d %d %d %.17g %.17g %.17g %.17g %.17g %.17g %d %d %d %d %d %s\n", F.stock_fit, F.stock_disp, F.stock_shaded, F.stock_shape, F.stock_sel,
                F.stock[0], F.stock[1], F.stock[2], F.stock_org[0], F.stock_org[1], F.stock_org[2], F.stock_axis, F.stock_center,
                F.stock_plane_dirty ? F.stock_vid : 0, F.stock_solid, F.stock_mtree, F.stock_file);   /* + plane (0 = as stored), solid id, Use Machine Tree, STL file (2026-10-22) */
    for (int i = 0; i < F.nsolids; i++)                      /* a part solid's description / level / colour changed here (Analyze's solid dialog; ours) */
        if (F.solids[i].brep) {                          /* an imported solid (STEP; no Parasolid XT): OCCSOLID + its BRep text in hex (ours) */
            fprintf(f, "OCCSOLID %s %d %d %zu %s\n", F.solids[i].name, F.solids[i].level, F.solids[i].color, F.solids[i].brep_n, F.solids[i].desc);
            for (size_t o = 0; o < F.solids[i].brep_n; o += 400) { fputs("BH ", f); for (size_t k = o; k < F.solids[i].brep_n && k < o + 400; k++) fprintf(f, "%02x", F.solids[i].brep[k]); fputc('\n', f); }
        } else if (F.solids[i].edited) fprintf(f, "SOLIDATTR %s %d %d %s\n", F.solids[i].name, F.solids[i].level, F.solids[i].color, F.solids[i].desc);
    gp_write_project(f);                                     /* Files / Tool Settings changed here (cui_grpprops.c; ours: the project lines) */
    for (int i = 0; i < F.nops; i++) if (F.ops[i].regen) fprintf(f, "REGEN %d\n", F.ops[i].id);   /* Mark2dHSTOpsRefMachStockDef4Regen marks (ours: the project line) */
    fprintf(f, "MACHINE %s\n", machine);
    fprintf(f, "TOOLS %d\n", F.ntools);
    for (int i = 0; i < F.ntools; i++) tool_write(f, &F.tools[i]);
    int ok = cam_write_ops(f, camops, ncamops, &F);
    /* the Operations Manager (= program) order: C<k> = the k-th op written above, S<id> = Mastercam's stored op */
    fprintf(f, "TREE");
    for (int i = 0; i < F.nops; i++) {
        int k = -1; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) k = j;
        if (k >= 0) fprintf(f, " C%d", k); else fprintf(f, " S%d", F.ops[i].id);
    }
    fprintf(f, "\n");
    for (int i = 0; i < F.nops; i++) if (F.ops[i].stale) fprintf(f, "STALE %d\n", F.ops[i].id);   /* tool changed after Mastercam made it */
    dirty_write_project(f);                                  /* dirty ops and their changed parameters (cui_dirty.c; ours: DIRTY lines) */
    for (int i = 0; i < F.nops; i++) if (F.ops[i].nrev) fprintf(f, "REVERSED %d %d\n", F.ops[i].id, F.ops[i].nrev);   /* Reverse toolpath, replayed on open */
    /* batch 11: post values of a Mastercam op edited here (cui_ops.c pp_ok_apply_postvals): groups, then the record bytes 0x398-0x399,
       0x7e8-0x867, 0xb50-0xb79 in hex; replayed on open after the reversals */
    for (int i = 0; i < F.nops; i++) if (F.ops[i].postv && F.ops[i].raw && F.ops[i].raw_n >= PV_CAN + 40) {
        static const int R[3][2] = { { PV_COOL_OLD, 2 }, { PV_MISC_SET, PV_MISC_REAL + 80 - PV_MISC_SET }, { PV_CAN_ON, PV_CAN + 40 - PV_CAN_ON } };
        fprintf(f, "POSTVALS %d %d", F.ops[i].id, F.ops[i].postv_groups);
        for (int q = 0; q < 3; q++) { fputc(' ', f); for (int b = 0; b < R[q][1]; b++) fprintf(f, "%02x", F.ops[i].raw[R[q][0] + b]); }
        fputc('\n', f);
    }
    if (fflush(f) != 0 || file_sync(f) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); cui_message("Save failed", "Writing %s failed: %s", path, strerror(errno)); return 0; }
    if (file_exists(path)) { remove(bak); rename(path, bak); }
    if (rename(tmp, path) != 0) { cui_message("Save failed", "Cannot replace %s: %s", path, strerror(errno)); return 0; }
    return 1;
}

typedef struct { int *ord, n, base; } DirtyCtx;
static CamOp *dirty_camop_of_k(int k, void *ctx) {        /* the op of ours written k-th (TREE's C<k>) */
    DirtyCtx *c = ctx;
    for (int i = 0; i < c->n; i++) if (c->ord && c->ord[i] == k && c->base + i < ncamops) return camops[c->base + i];
    return NULL;
}
/* read a project (or legacy sidecar). part_override: the part to load instead of the PART line. */
int doc_read(const char *proj, const char *part_override) {
    FILE *f = fopen(proj, "r");
    if (!f) { cui_message("Open failed", "Cannot open %s: %s", proj, strerror(errno)); return 0; }
    char part[1100] = "", line[1 << 16];
    int nop_lines = 0, has_tools = 0, is_proj = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "MCAMUI-PROJECT", 14)) is_proj = 1;
        else if (!strncmp(line, "PART ", 5)) CP(part, line + 5);
        else if (!strncmp(line, "MACHINE ", 8)) CP(machine, line + 8);
        else if (!strncmp(line, "TOOLS ", 6)) has_tools = 1;
        else if (!strncmp(line, "OP ", 3)) nop_lines++;
    }
    if (!is_proj && !nop_lines) { fclose(f); cui_message("Open failed", "%s is not a CAM workbench project.", proj); return 0; }
    if (part_override) CP(part, part_override);
    int newpart = 0;
    rewind(f);
    while (fgets(line, sizeof line, f)) if (!strncmp(line, "NEWPART", 7)) newpart = 1;
    if (!part[0] && !newpart && ends_with_ci(proj, ".mcamui")) { snprintf(part, sizeof part, "%.*s", (int)(strlen(proj) - 7), proj); }
    if (newpart) new_file();
    else if (!part[0] || !load_file(part)) {
        fclose(f);
        if (part[0] && mcam_error_code() == MCAM_ERR_NEWER) cui_message("Open failed", "%s", status);   /* a newer release's part: Mastercam's message alone (mcam_grp.h) */
        else cui_message("Open failed", "The project's part could not be loaded:\n%s\n\n%s", part[0] ? part : "(no PART line)", status);
        return 0;
    }
    if (!newpart) geo_level = F.main_level;          /* set_main_level of the part's level record (FUN_1802d5220 L83) */
    {   /* PROJECT-FORMAT (ours): an older project's entity indices on a part with notes (DOC_FORMAT above) */
        int fmt = 1; rewind(f);
        while (fgets(line, sizeof line, f)) if (!strncmp(line, "PROJECT-FORMAT ", 15)) fmt = atoi(line + 15);
        cam_load_remap_from = cam_load_remap_by = 0;
        if (fmt < 2) {
            int first = -1, nn = 0;
            for (int i = 0; i < F.nents; i++) if (F.ents[i].type == E_NOTE && !F.ents[i].created) { if (first < 0) first = i; nn++; }
            if (nn) { cam_load_remap_from = first; cam_load_remap_by = nn; }
        }
    }
    {   /* UNITS: the units change made before the project was saved (doc_write), before any geometry / operation is replayed */
        int u = -1; rewind(f);
        while (fgets(line, sizeof line, f)) if (!strncmp(line, "UNITS ", 6)) u = atoi(line + 6);
        if (u == 0 || u == 1) cui_units_set(u, 0);
        else if (newpart && !mcam_is_metric(&F)) cui_units_set(1, 0);   /* a new part's project without UNITS: metric */
    }
    doc_clear_ops();
    planes_restore_part();                                /* before the project's own UPLANE / ACTPLANE lines below (cui_planes.c) */
    {   /* geometry drawn / deleted here, applied in order so the chains' entity indices match */
        rewind(f); int ng = 0;
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "GEO ", 4)) {
                int ty, lv, co, vw = 0, pos = 0; V3 a, b, c; double r, a0, sw, M[9];
                if (sscanf(line + 4, "%d %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %d %d%n %d%n", &ty, &a.x, &a.y, &a.z, &b.x, &b.y, &b.z, &c.x, &c.y, &c.z, &r, &a0, &sw, &lv, &co, &pos, &vw, &pos) >= 15) {
                    int k, nm = 0;
                    if (ty == E_ARC && vw >= 2) {              /* the matrix written after the view number (this format), else the reserved view's */
                        const char *q = line + 4 + pos; char *e2;
                        for (nm = 0; nm < 9; nm++) { M[nm] = strtod(q, &e2); if (e2 == q) break; q = e2; }
                        if (nm < 9 && !mcam_std_view(vw, M)) vw = 0;   /* an older line with a view that is not reserved: as XY (ours) */
                    }
                    if (ty == E_ARC && vw >= 2) k = mcam_new_arc_view_w(&F, vw, M, c, r, a0, sw, lv, co);
                    else k = mcam_new_ent(&F, ty ? ty : E_LINE, a, b, c, r, a0, sw, lv, co);
                    if (!ty) mcam_delete_ent(&F, k);
                    ng++;
                }
            } else if (!strncmp(line, "SURF ", 5) || !strncmp(line, "SURFEDIT ", 9)) {
                int ed = line[4] == 'E'; unsigned n = 0;
                free(sr_buf); sr_buf = NULL;
                if (sscanf(line + (ed ? 9 : 5), "%d %d %u", &sr_a, &sr_b, &n) == 3 && n >= 0x68 + 0x98 && n < (1u << 28)) { sr_buf = calloc(1, n); sr_n = n; sr_at = 0; sr_edit = ed; }
                if (!ed) ng++;
            } else if (!strncmp(line, "MESH ", 5)) {            /* a mesh made here: MESH level colour count mixed + MH lines (doc_write) */
                int np = 0; free(mh_buf); mh_buf = NULL;
                if (sscanf(line + 5, "%d %d %d %d", &mh_lv, &mh_co, &np, &mh_mixed) == 4 && np > 0 && np < (1 << 24)) { mh_np = np; mh_n = 0x130 + (size_t)np * 49; mh_at = 0; mh_buf = calloc(1, mh_n); }
            } else if (!strncmp(line, "MH ", 3)) {
                for (const char *q = line + 3; mh_buf && mh_at < mh_n && q[0] && q[1] && q[0] != '\n'; q += 2) { unsigned v; if (sscanf(q, "%2x", &v) != 1) break; mh_buf[mh_at++] = (uint8_t)v; }
                if (mh_buf && mh_at == mh_n) {
                    StlPoly *pp = calloc((size_t)mh_np, sizeof *pp);
                    for (int k = 0; k < mh_np; k++) { const uint8_t *q = mh_buf + 0x130 + (size_t)k * 49; memcpy(pp[k].n, q, 12); memcpy(pp[k].v, q + 12, 36); pp[k].attr = q[48]; }
                    int k = mcam_new_mesh(&F, mh_buf, pp, mh_np, mh_mixed, mh_lv < 0 ? 1 : mh_lv, mh_co);
                    if (mh_lv < 0) mcam_delete_ent(&F, k);
                    free(mh_buf); mh_buf = NULL; ng++;
                }
            } else if (!strncmp(line, "SH ", 3) && cv_buf) {    /* the hex of a CURVE: ends with its byte count */
                const char *h = line + 3; for (; h[0] && h[0] != '\n' && h[0] != '\r' && cv_at < cv_n; h += 2) { int v; sscanf(h, "%2x", &v); cv_buf[cv_at++] = (uint8_t)v; }
                if (cv_at >= cv_n) { int k = mcam_new_curve(&F, cv_buf, cv_n, cv_lv, cv_co); (void)k; free(cv_buf); cv_buf = NULL; ng++; }
            } else if (!strncmp(line, "SH ", 3)) {
                for (const char *q = line + 3; sr_buf && sr_at < sr_n && q[0] && q[1] && q[0] != '\n'; q += 2) { unsigned v; if (sscanf(q, "%2x", &v) != 1) break; sr_buf[sr_at++] = (uint8_t)v; }
                if (sr_buf && sr_at == sr_n) surf_read_done();
            } else if (!strncmp(line, "STOCK ", 6)) {
                int a[10] = { 0 }, at = 0; double v[6];   /* axis / centre on axis since 2026-10-22 (older lines have 11 fields); plane, solid, Use Machine Tree, file (16 + file) */
                int nf = sscanf(line + 6, "%d %d %d %d %d %lf %lf %lf %lf %lf %lf %d %d %d %d %d %n", &a[0], &a[1], &a[2], &a[3], &a[4], &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &a[5], &a[6], &a[7], &a[8], &a[9], &at);
                if (nf >= 11) {
                    F.stock_axis = a[5]; F.stock_center = a[6];
                    if (nf >= 16) {
                        F.stock_solid = a[8]; F.stock_mtree = a[9];
                        if (at > 0) { snprintf(F.stock_file, sizeof F.stock_file, "%s", line + 6 + at); F.stock_file[strcspn(F.stock_file, "\r\n")] = 0; }
                        if (a[7] >= 1 && a[7] <= 7 && view_std_matrix(a[7], F.stock_vm)) { F.stock_rec = 1; F.stock_vid = F.stock_vno = a[7]; F.stock_vo[0] = F.stock_vo[1] = F.stock_vo[2] = 0; F.stock_plane_dirty = 1; }
                    }
                    F.stock_fit = a[0]; F.stock_disp = a[1]; F.stock_shaded = a[2]; F.stock_shape = a[3]; F.stock_sel = a[4];
                    for (int k = 0; k < 3; k++) { F.stock[k] = v[k]; F.stock_org[k] = v[3 + k]; }
                    if (!F.stock_rec) { F.stock_rec = 1; F.stock_vid = F.stock_vno = 1; for (int k = 0; k < 9; k++) F.stock_vm[k] = k % 4 == 0; }
                    stock_calc_store();
                    F.stock_dirty = 1;
                }
            } else if (!strncmp(line, "SOLIDATTR ", 10)) {  /* SOLIDATTR sld_N level colour description (ours) */
                char nm[64]; int lv, co, at = 0;
                if (sscanf(line + 10, "%63s %d %d %n", nm, &lv, &co, &at) >= 3)
                    for (int i = 0; i < F.nsolids; i++) if (!strcmp(F.solids[i].name, nm)) {
                        F.solids[i].level = lv; F.solids[i].color = co; F.solids[i].edited = 1;
                        snprintf(F.solids[i].desc, sizeof F.solids[i].desc, "%s", at > 0 ? line + 10 + at : "");
                        F.solids[i].desc[strcspn(F.solids[i].desc, "\r\n")] = 0;
                    }
            } else if (gp_read_project_line(line)) {         /* GRPSET / GRPNAME / GRPCMT (cui_grpprops.c) */
            } else if (!strncmp(line, "REGEN ", 6)) {
                int id = atoi(line + 6); for (int i = 0; i < F.nops; i++) if (F.ops[i].id == id) F.ops[i].regen = 1;
            } else if (!strncmp(line, "UPLANE ", 7)) {         /* ours: a plane made here; ids kept as written */
                double m[9], o[3]; int id = 0, pos = 0;
                if (sscanf(line + 7, "%d %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf%n", &id, &m[0], &m[1], &m[2], &m[3], &m[4], &m[5], &m[6], &m[7], &m[8], &o[0], &o[1], &o[2], &pos) == 13
                    && id > 7 && nuplanes < UPLANE_MAX && cam_plane_register(id, m)) {
                    char *nm = line + 7 + pos; if (*nm == ' ') nm++; nm[strcspn(nm, "\r\n")] = 0;
                    UPlane *u = &uplanes[nuplanes++]; memset(u, 0, sizeof *u); u->id = id; snprintf(u->name, sizeof u->name, "%s", nm);
                    memcpy(u->m, m, sizeof m); memcpy(u->org, o, sizeof o); u->wofs = -1;
                }
            } else if (!strncmp(line, "ACTPLANE ", 9)) {
                int a[4], o[3], n = sscanf(line + 9, "%d %d %d %d %d %d %d", &a[0], &a[1], &a[2], &a[3], &o[0], &o[1], &o[2]);
                if (n >= 4) for (int t = 0; t < 4; t++) plane_active[t] = a[t] >= 1 || (t == 2 && a[t] == 0) ? a[t] : 1;
                if (n == 7) { plane_set_active_org(0, o[0]); plane_set_active_org(1, o[1]); plane_set_active_org(3, o[2]); }
                else if (n >= 4) for (int t = 0; t < 4; t++) if (t != 2) plane_set_active_org(t, plane_active[t]);   /* a line from before the origins were written: SetActivePlane's origin */
            } else if (dr_read_line(line)) {                 /* DIM: a drafting entity made here (cui_draft.c) */
            } else if (dn_read_line(line)) {                 /* NOTE: a note made here (cui_draft_note.c) */
            } else if (!strncmp(line, "CURVE ", 6)) {          /* a spline made here (the writer above) */
                int lv, co; unsigned nb; if (sscanf(line + 6, "%d %d %u", &lv, &co, &nb) == 3) { cv_buf = malloc(nb ? nb : 1); cv_n = nb; cv_at = 0; cv_lv = lv; cv_co = co; }
            } else if (!strncmp(line, "OCCSOLID ", 9)) {        /* an imported solid: name level colour bytes description */
                char nm[64]; int lv, co, pos = 0; size_t nb;
                if (sscanf(line + 9, "%63s %d %d %zu%n", nm, &lv, &co, &nb, &pos) == 4) {
                    F.solids = realloc(F.solids, (F.nsolids + 1) * sizeof(Solid)); Solid *S = &F.solids[F.nsolids++]; memset(S, 0, sizeof *S);
                    snprintf(S->name, sizeof S->name, "%s", nm); S->level = lv; S->color = co; S->brep_n = nb; S->brep = malloc(nb + 1); bh_at = 0; bh_i = F.nsolids - 1;
                    const char *d = line + 9 + pos; while (*d == ' ') d++; snprintf(S->desc, sizeof S->desc, "%s", d); S->desc[strcspn(S->desc, "\r\n")] = 0;
                }
            } else if (!strncmp(line, "BH ", 3)) {
                if (bh_i >= 0 && bh_i < F.nsolids) { const char *h = line + 3; Solid *S = &F.solids[bh_i]; for (; h[0] && h[0] != '\n' && h[0] != '\r' && bh_at < S->brep_n; h += 2) { int v; sscanf(h, "%2x", &v); S->brep[bh_at++] = (uint8_t)v; } }
            } else if (!strncmp(line, "GEOATTR ", 8)) {         /* the entity just read */
                int ls, bl; double w; int dummy;
                if (sscanf(line + 8, "%d %d %lf %d", &dummy, &ls, &w, &bl) == 4 && F.nents > 0) { Ent *en = &F.ents[F.nents - 1]; en->ln_style = ls; en->width = w; if (bl) mcam_ent_blank(&F, F.nents - 1, 1); }
            } else if (!strncmp(line, "DEL ", 4)) {
                int idn = atoi(line + 4);
                for (int i = 0; i < F.nents; i++) if (F.ents[i].idn == idn && !F.ents[i].created) mcam_delete_ent(&F, i);
            }
        }
        if (ng || newpart) { mcam_update_extents(&F); scene_reupload(); }
        if (F.ndnew) dr_scene();
        feats_read(f);
        if (nfeats) solids_rebuild();
    }
    if (has_tools) {                                     /* the project's tool list replaces the part's */
        for (int i = 0; i < F.ntools; i++) free(F.tools[i].holder_rz);
        F.ntools = 0;
        rewind(f);
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "TOOL ", 5) && F.ntools < 256) { if (tool_parse(line, &F.tools[F.ntools])) {
                if (!strstr(line, " is_metric=")) F.tools[F.ntools].is_metric = mcam_is_metric(&F);   /* projects written before it was stored */
                F.ntools++; } }
            else if (!strncmp(line, "THOLDER ", 8) && F.ntools) tool_read_holder(line, &F.tools[F.ntools - 1]);
        }
    }
    fclose(f);
    CamOp **ops; int *ord = NULL, base = ncamops; int n = cam_load_ops_ord(proj, &F, &ops, &ord);
    for (int i = 0; i < n; i++) if (ops[i]->p.file_op_id > 0) remove_stored_op(ops[i]->p.file_op_id);
    for (int i = 0; i < n && ncamops < MAX_CAMOPS; i++) {
        ops[i]->op_index = mcam_add_op(&F, cam_type_label(ops[i]->p.type), ops[i]->name, ops[i]->p.tool_num, ops[i]->m, ops[i]->nm);
        camops[ncamops++] = ops[i];
        if (!mcam_tool(&F, ops[i]->p.tool_num) && F.ntools < 256) {   /* legacy sidecar: tool only known by diameter */
            tool_new(&F.tools[F.ntools], ops[i]->p.tool_type ? ops[i]->p.tool_type : 10, ops[i]->p.tool_dia, ops[i]->p.tool_num, mcam_is_metric(&F));
            F.tools[F.ntools].rpm = ops[i]->p.rpm; F.tools[F.ntools].feed = ops[i]->p.feed; F.tools[F.ntools].plunge = ops[i]->p.plunge_feed;
            F.ntools++;
        }
    }
    free(ops);
    {   /* TREE / STALE lines: restore the program order and the stale marks */
        FILE *g = fopen(proj, "r"); char *ln = NULL; size_t lcap = 0;
        while (g && read_line(g, &ln, &lcap)) {
            if (!strncmp(ln, "STALE ", 6)) { int id = atoi(ln + 6); for (int i = 0; i < F.nops; i++) if (F.ops[i].id == id) F.ops[i].stale = 1; }
            if (!strncmp(ln, "REVERSED ", 9)) { int id = 0, k = 0; char rm[300]; sscanf(ln + 9, "%d %d", &id, &k);   /* the same NCI -> the same result */
                for (int i = 0; i < F.nops; i++) if (F.ops[i].id == id) for (int r = 0; r < k && r < 64; r++) nci_reverse_op(&F, i, rm, sizeof rm); }
            if (!strncmp(ln, "POSTVALS ", 9)) {   /* batch 11: the same record bytes -> the same NCI records */
                static const int R[3][2] = { { PV_COOL_OLD, 2 }, { PV_MISC_SET, PV_MISC_REAL + 80 - PV_MISC_SET }, { PV_CAN_ON, PV_CAN + 40 - PV_CAN_ON } };
                int id = 0, g = 0, pos = 0; char rm[300];
                if (sscanf(ln + 9, "%d %d %n", &id, &g, &pos) >= 2)
                    for (int i = 0; i < F.nops; i++) if (F.ops[i].id == id && F.ops[i].raw && F.ops[i].raw_n >= PV_CAN + 40) {
                        const char *h = ln + 9 + pos; int bad = 0;
                        for (int q = 0; q < 3 && !bad; q++) { while (*h == ' ') h++;
                            for (int b = 0; b < R[q][1]; b++) { unsigned v; if (sscanf(h, "%2x", &v) != 1) { bad = 1; break; } F.ops[i].raw[R[q][0] + b] = (uint8_t)v; h += 2; } }
                        if (!bad && pv_nci_rewrite(&F, i, g, rm, sizeof rm)) F.ops[i].postv_groups = g;
                    }
            }
            if (strncmp(ln, "TREE", 4)) continue;
            int ids[1024], nid = 0;                          /* the F.ops id of each camop, to find it again after moving */
            for (int j = 0; j < ncamops && j < 1024; j++) ids[j] = camops[j]->op_index >= 0 ? F.ops[camops[j]->op_index].id : -1;
            int pos = 0; (void)nid;
            for (char *t = strtok(ln + 4, " "); t && pos < F.nops; t = strtok(NULL, " ")) {
                int cur = -1;
                if (t[0] == 'S') { int id = atoi(t + 1); for (int i = 0; i < F.nops; i++) if (F.ops[i].id == id) cur = i; }
                else if (t[0] == 'C') { int k = atoi(t + 1); for (int i = 0; i < n; i++) if (ord && ord[i] == k && base + i < ncamops) cur = camops[base + i]->op_index; }
                if (cur < pos) continue;                     /* not here (failed / dropped) or already placed */
                mcam_move_op(&F, cur, pos++);
            }
            for (int j = 0; j < ncamops && j < 1024; j++) if (ids[j] >= 0) for (int i = 0; i < F.nops; i++) if (F.ops[i].id == ids[j]) camops[j]->op_index = i;
        }
        if (g) fclose(g);
        free(ln);
        DirtyCtx dc = { ord, n, base };
        dirty_read_project(proj, dirty_camop_of_k, &dc);   /* DIRTY C<k> / S<id> + their D: blocks (cui_dirty.c) */
        free(ord);
    }
    cam_load_remap_from = cam_load_remap_by = 0;
    scene_reupload();
    if (n < nop_lines) cui_message("Open", "%d of %d operation(s) could not be regenerated (the part geometry changed?) and were dropped.", nop_lines - n, nop_lines);
    { int anyb = 0; for (int i = 0; i < F.nsolids; i++) if (F.solids[i].brep) anyb = 1; if (anyb) { scene_remesh_solids(); scene_reupload(); view_fit(0); } }   /* imported solids read from the project */
    cam_msg[0] = 0;                                      /* opening is silent (title bar shows the file; item 17) */
    overlay_dirty = 1;
    return 1;
}

/* a yes/no or integer config value of key (the Converters page's keys, FUN_1801d3ec0), else dflt */
static int cad_cfg(int key, int dflt) {
    char b[64]; mcam_config_value(1, key, b, sizeof b);
    if (!b[0]) return dflt;
    if (b[0] == 'y' || b[0] == 'Y') return 1;
    if (b[0] == 'n' || b[0] == 'N') return 0;
    return atoi(b);
}
/* FUN_180345830, the Open route of a converter's file: init_db (a new empty part, the system units kept), DoImport into it, then the report. A failed import leaves the
   empty part (init_db ran first in the original) */
static int open_cad(const char *path, int kind) {
    if (kind == CAD_DWG) { cui_message("Open", "%s: DWG / DWF files are not read here (Mastercam reads them with the Teigha library, which is not available): NOT DECODED.", path); return 0; }
    doc_clear_ops(); new_file(); doc_path[0] = 0;
    { CP(cad_doc_name, path); char *d = strrchr(cad_doc_name, '.'); if (d) strcpy(d, ".mcam"); }
    CadImportOpts o;
    o.get_solids = cad_cfg(3003, 1); o.get_edges = cad_cfg(3000, 0); o.repair = cad_cfg(3001, 0); o.stitch = cad_cfg(3011, 0); o.bad_surf_level = cad_cfg(3002, 10000);
    o.blank_ps = cad_cfg(3007, 1); o.break_df = cad_cfg(3008, 0); o.iges_tol = cad_cfg(3009, 0); o.iges_units = cad_cfg(3010, 1); o.ignore_colors = cad_cfg(3020, 0);
    o.stl_lines = cad_cfg(3019, 0);                        /* convSTLImportLines: default off = STL mesh (FUN_1803babd0, ~/mastercam-re/work/cadimp/dump/core_conv.c) */
    cad_tol_setup(mc_systol_units(mcam_is_metric(&F))->tol);
    char msg[2000] = "";
    int ok = cad_import_file(&F, path, kind, &o, geo_color, geo_level, msg, sizeof msg);
    F.main_level = geo_level;
    scene_remesh_solids(); scene_reupload(); view_fit(0);
    if (!ok) { cui_message("Open", "%s", msg); return 0; }
    cam_msg[0] = 0;
    if (strstr(msg, "Not imported:")) cui_message("Open", "%s\n\n%s", path, msg);
    return 1;
}

int open_part(const char *p) {
    if (!p || !p[0]) return 0;
    char path[1100]; CP(path, p);
    if (!file_exists(path)) { cui_message("Open failed", "%s does not exist.", path); return 0; }
    int ok;
    int cad = cad_import_kind(path);
    if (cad) {
        ok = open_cad(path, cad);
        if (!ok) return 0;
        doc_dirty = 1; overlay_dirty = 1; sm_psolids_load(); recent_add(path);
        return 1;
    }
    if (is_project_file(path)) { ok = doc_read(path, NULL); if (ok) CP(doc_path, path); }
    else {
        char side[1200]; snprintf(side, sizeof side, "%s.mcamui", path);
        if (file_exists(side)) { ok = doc_read(side, path); if (ok) CP(doc_path, side); }
        else {
            if ((ok = load_file(path))) { doc_clear_ops(); planes_restore_part(); doc_path[0] = 0; cam_msg[0] = 0; geo_level = F.main_level;   /* set_main_level (FUN_1802d5220 L83) */
                scene_reupload(); view_fit(0); }          /* load_file uploaded (and fitted) the scene before doc_clear_ops dropped the previous
                                                             document's solids (an extruded solid stayed drawn over the new part) */
            else cui_message("Open failed", "%s", status);
        }
    }
    if (!ok) return 0;                                    /* the previous document is untouched */
    planes_restore_view();                                /* the construction depth, Gview and window of the part's view record (cui_planes.c) */
    doc_dirty = 0; overlay_dirty = 1;                     /* (mcamui also leaves the backstage: its caller) */
    sm_psolids_load();                                    /* the Solids manager's solid rows: expanded as stored (ops header +0x0e) */
    recent_add(path);
    return 1;
}

void doc_close(void) {
    doc_clear_ops(); unload_file();
    doc_path[0] = 0; doc_dirty = 0; cam_msg[0] = 0; overlay_dirty = 1;
}

/* doc_save_to (mcamui.c): the project is written with the .mcamui extension; saving is silent (item 17) */
int cui_doc_save_to(const char *path) {
    char p[1100]; CP(p, path);
    if (!is_project_file(p)) { size_t n = strlen(p); snprintf(p + n, sizeof p - n, ".mcamui"); }
    if (!doc_write(p)) return 0;
    CP(doc_path, p); doc_dirty = 0; recent_add(p);
    return 1;
}

/* doc_save (mcamui.c): Save writes the open project, else PART.mcamui beside the part; Save As (and an untitled
   document) asks with the dialog at the project's / part's folder and name (untitled.mcamui in the last recent file's
   folder for a new part) */
int cui_doc_save_target(int as, char *path, size_t np, char *dir, size_t nd, char *name, size_t nn) {
    if (!loaded && !ncamops) { cui_message("Save", "Nothing to save: open a part first."); return -1; }
    if (!as && doc_path[0]) { snprintf(path, np, "%s", doc_path); return 1; }
    if (!as && loaded && F.path[0]) { snprintf(path, np, "%s.mcamui", F.path); return 1; }
    snprintf(dir, nd, "."); snprintf(name, nn, "project.mcamui");
    const char *src = doc_path[0] ? doc_path : loaded ? F.path : "";
    if (!src[0]) { snprintf(name, nn, "untitled.mcamui"); if (nrecent_files) { const char *sl = path_sep_r(recent_files[0]); if (sl) snprintf(dir, nd, "%.*s", (int)(sl - recent_files[0]), recent_files[0]); } }
    if (src[0]) {
        const char *sl = path_sep_r(src);
        if (sl) snprintf(dir, nd, "%.*s", (int)(sl - src), src);
        snprintf(name, nn, "%s", sl ? sl + 1 : src);
        if (!is_project_file(name)) { char *d = strrchr(name, '.'); if (d) *d = 0; strncat(name, ".mcamui", nn - strlen(name) - 1); }
    }
    return 0;
}

void remove_stored_op(int id) {
    for (int i = 0; i < F.nops; i++) {
        if (F.ops[i].id != id) continue;
        int mine = 0; for (int j = 0; j < ncamops; j++) if (camops[j]->op_index == i) mine = 1;
        if (mine) return;
        dirty_forget_stored(id);                         /* its changed parameters (cui_dirty.c) go with it */
        mcam_remove_op(&F, i);
        for (int j = 0; j < ncamops; j++) if (camops[j]->op_index > i) camops[j]->op_index--;
        if (sel == i) sel = -1; else if (sel > i) sel--;
        msel_owner = -9;
        if (cui_hooks.ops_changed) cui_hooks.ops_changed();
        return;
    }
}
