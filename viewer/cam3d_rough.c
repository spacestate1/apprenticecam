/* cam3d_rough.c - 3D roughing: Surface High Speed "Dynamic OptiRough" (op 132, style byte record 0x16bc = 1).
 * Included by cam2d.c (like cam2d_dyn.c: it uses MB / mv, the Clipper wrapper and the ported mcce engine).
 *
 * The decode is formats/hst3d_port_plan.md section 4 (cited "plan 4.x"); the McHmm / mckernel / mcmsurf decompiles are
 * decomp/hmm_zplan.c, decomp/hst3d_weave_drop.c, decomp/hst3d_grid.c, decomp/mckernel_all.c, decomp/mcmsurf_all.c.
 * What runs here, in Mastercam's order (plan 4.1):
 *   1. the drive model as triangles (McHmm slices the tessellation only, plan 4.8). The tessellator is OCCT's BRepMesh on
 *      the XT -> OCCT translation (occt_mesh.cpp; decision 3D-2, ours);
 *   2. the Z levels of EvaZProfilePlan (FUN_18040da70, hmm_zplan.c l.13173; plan 4.3), from PosZHi down to PosZLo;
 *   3. at each level the slice of the model with the tool ("weave", plan 4.5): the tool-centre positions where the tool
 *      at that level collides with a triangle, on X and Y fibres spaced MachWeaveResolution (FUN_180310790 default
 *      expressions, hmm_pass0.c l.723-799), tool radius-at-height r(h) (FUN_18013fff0, hst3d_grid.c l.4060) with the
 *      ball part VorProfileFlatRad removed (the WeaveCutterRadius expression) and the flat radius put back as a 2D offset;
 *   4. the regions the tool may clear = the containment minus that slice; each region boundary is offset by the tool
 *      radius (FUN_180107dcc called with the tool diameter x 0.5 in the executioner FUN_180106624, mckernel_all.c
 *      l.203236) and handed to mcce::MCCECalcDynPock (viewer/mcce_dyn.c, not changed) with the CDynPockParm of
 *      FUN_180108f1c (mckernel_all.c l.205368: depth +0x80 = the level, top +0x78);
 *   5. the moves, links between regions / levels by the neutral SAFE rule (decision 3D-4, ours, as 2D Area Clearance:
 *      formats/improvisation_review.md CAM item 8).
 * Ours (labelled where used): the tessellator; the polygon from the fibre intervals (the CWeave graph traversal
 * FUN_18012d020 and on is NOT DECODED: here the intervals of neighbouring fibres are joined into quadrilaterals and
 * united - the vertices are the decoded contact positions, the joining between them is ours); the push-cutter's
 * per-feature formulas (FUN_180125dd0 / 180126340 / 180126130 / 180126e90 / 180127d50 compute the interval where the
 * tool's profile bands touch a vertex / edge / facet: the same set is computed here in closed form for the
 * sphere + cylinder shape the weave uses); the default containment (the model's XY box) and the SAFE links.
 * NOT DECODED (refused or left out, never guessed): step-up levels on flats (StepUpEnable: refused), "Skip pockets
 * smaller than" (record 0x13e8: not applied), Core (from outside, record 0x16ef = TP+0x486), the
 * 2D stock model between levels (each level clears its whole region), the Imported Linking Plan, adaptive levels (OptiRough
 * passes adaptive = 0, hmm_zplan.c l.16489). Inflexion levels are ported (c3_levels_infl). */

#include <time.h>
#include "occt_mesh.h"
#include "occt_cad.h"
#include "mcsurf.h"
#include "xt_mesh.h"
#include "sldtoolp.h"
#include "cam3d_mmodel.h"   /* mm_hst_surface: the HST drive mesh (MCAM_HST_MESH) */

/* ---------------------------------------------------------------------------------------------- the drive mesh */
/* attr: with MCAM_HST_MESH (c3_mesh_refs) the 2-byte attribute FUN_180258310 writes after each triangle of tmphardmillraw.stl, else NULL */
typedef struct { double *t; int n; double lo[3], hi[3]; unsigned short *attr; } C3Mesh;
static void c3_mesh_free(C3Mesh *m) { free(m->t); free(m->attr); memset(m, 0, sizeof *m); }
static void c3_mesh_box(C3Mesh *m) {
    for (int k = 0; k < 3; k++) { m->lo[k] = 1e300; m->hi[k] = -1e300; }
    for (int i = 0; i < m->n * 3; i++) for (int k = 0; k < 3; k++) { double v = m->t[3 * i + k]; if (v < m->lo[k]) m->lo[k] = v; if (v > m->hi[k]) m->hi[k] = v; }
}
/* ours (testing): an ASCII / binary STL as the drive model (cam_cli opti-stl), in part units */
int cam3d_stl_load(const char *path, double **tri) {
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc(sz + 1); if (!d || fread(d, 1, sz, f) != (size_t)sz) { fclose(f); free(d); return -1; }
    fclose(f); d[sz] = 0;
    int n = 0, cap = 0; double *t = NULL;
    uint32_t nb = sz >= 84 ? (uint32_t)(d[80] | d[81] << 8 | d[82] << 16 | (uint32_t)d[83] << 24) : 0;
    if (sz >= 84 && 84 + (long)nb * 50 == sz) {
        t = malloc(sizeof(double) * 9 * (nb ? nb : 1)); n = nb;
        for (uint32_t i = 0; i < nb; i++) for (int k = 0; k < 9; k++) { float v; memcpy(&v, d + 84 + i * 50 + 12 + 4 * k, 4); t[9 * i + k] = v; }
    } else {
        char *s = (char *)d; int nv = 0;
        while ((s = strstr(s, "vertex"))) {
            double x, y, z; char *e1, *e2, *e3; s += 6;   /* strtod: sscanf on the long buffer would scan its length every call */
            x = strtod(s, &e1); y = strtod(e1, &e2); z = strtod(e2, &e3);
            if (e1 == s || e2 == e1 || e3 == e2) continue;
            s = e3;
            if (nv % 3 == 0 && n == cap) { cap = cap ? cap * 2 : 1024; t = realloc(t, sizeof(double) * 9 * cap); }
            t[9 * n + 3 * (nv % 3)] = x; t[9 * n + 3 * (nv % 3) + 1] = y; t[9 * n + 3 * (nv % 3) + 2] = z;
            if (++nv % 3 == 0) n++;
        }
    }
    free(d); *tri = t; return n;
}
static const char *g_c3_stl;            /* ours (testing): cam3d_set_stl */
void cam3d_set_stl(const char *path) { g_c3_stl = path; }

/* a stored solid reference's role (formats/surface_selection.md 2.2 / 2.3 / 7): its faces and their types live in the solid's SldToolPOp
   with the record's link id (viewer/sldtoolp.c). A whole body takes the body type +0x148 (0 drive, 4 check, 0x20 gouge;
   msurf_ent_count counts its faces under that type); single face picks are refused (user decision N13-7 / SS-5: which XT face a pick
   is needs Parasolid's history replay, NOT DECODED). A solid picked here (link 0) is a whole body, drive. Returns TP_REF_* or -1 + why. */
static int c3_solid_role(const Solid *s, int link, char *why, size_t whyn) {
    if (!link) return TP_REF_DRIVE;
    SldToolP t; char e[128] = "";
    int k = sldtoolp_get(s, link, &t, e);
    if (k < 0) { snprintf(why, whyn, "its toolpath selection (SldToolPOp, link %d) does not read: %s", link, e); return -1; }
    if (k == 0) { snprintf(why, whyn, "its history has no toolpath selection (SldToolPOp) with link %d", link); return -1; }
    int whole = t.whole, bt = t.body_type; sldtoolp_free(&t);
    if (!whole) { snprintf(why, whyn, "the operation picks single faces of it; the solid faces cannot be identified (NOT DECODED: needs Parasolid's history replay)"); return -1; }
    if (bt == 0) return TP_REF_DRIVE;
    if (bt == 4) return TP_REF_CHECK;
    if (bt == 0x20) return TP_REF_GOUGE;
    snprintf(why, whyn, "its whole-body type 0x%x is NOT DECODED", bt); return -1;
}
/* S7: what an op does with its check references (formats/surface_selection.md 8). bnci_surface (decomp/mcmsurf_all.c) with check
   surfaces present (local_6c80 = the check count from surf_sel_bits, l.12172-12202; DAT_1805034a1 = 1):
   - op 132 sets op +0x16c4 (record 0x172c = the kernel's K+0x495) per style (l.12212-12234): styles 0 Area Roughing, 1 OptiRough,
     3 Scallop, 4 Horizontal Area, 6 Pencil -> 0; 7 Hybrid -> 1; every other style -> mutlGetHstCheckTrimmingMethod() (l.376213: the
     registry value "HST check trimming method", 1 when absent - absent in the user's hive). With 0x16c4 == 0
     process_3rd_party_algorithm calls FUN_180258310(2, ..) (l.18212-18215), which writes **every** machine-model surface, drive
     and check (mach_model_surf +0x66: 1 = check, the same test as FUN_180290fb0 l.429259), into tmphardmillraw.stl (l.395794:
     `local_2f80 == 2 || type == local_2f80`) - the kernel's drive model, PosZHi / PosZLo included - and mckernel builds none of
     its check plans (K+0x495 == 0: mk l.92847, l.93569 ...). FUN_1800202d0 (l.23046) tells the user so: mcmsurfres 282 "Check
     geometry in HST/Roughing operation will be treated as drive geometry" (styles 0 / 1, l.23098-23115) / 287 (styles 3 / 4 / 6,
     l.23118-23129). -> C3_CHK_MERGE: the check references are meshed with the drive.
   - With 0x16c4 != 0 (Waterline, Raster, Spiral, Radial, Project by the registry default) FUN_180258310(0, ..) writes the drive
     surfaces only and mckernel loads the checks as their own model (FUN_18007ca68 mk l.92160: Check Database Loader 0x1421, Detect
     Top Check Surfaces 0x1423, Check Surface Tessellation 0x1424) and trims the passes against it with the Edit Passes to Surfaces
     plan 0x141f ("ProtectedSurfaces", OverThickness from FUN_180070e8c mk l.86832; mk l.93590-93630 ...) -> C3_CHK_KERNEL: ported
     (N13-10, 2026-11-13): the generators open the check model with c3_chk_open (cam3d_checktrim.c, formats/surface_selection.md 8.3) and
     trim every pass; c3_mesh_op then leaves the checks out of the drive. A generator that does not open it still refuses (c3_check_refuse).
   - ops 10 / 0x83 (Rough Pocket, Multisurface Pocket): no toolpath, mcmsurfres 249 "This operation cannot have check surfaces"
     (l.12645-12700: lVar25 = 0xf9 -> post_warning) -> C3_CHK_249.
   - the other classic ops build the check model (tmpmn4.tmp, formats/surf_rough.md 2.1) and project against it with the check
     stock (FUN_180290fb0 l.429259-429310); the per-surface contact solvers are NOT DECODED -> C3_CHK_CLASSIC: refused. */
enum { C3_CHK_MERGE, C3_CHK_KERNEL, C3_CHK_249, C3_CHK_CLASSIC };
static int c3_check_rule(int type) {
    switch (type) {
    case CAM_AREAROUGH: case CAM_OPTIROUGH: case CAM_SCALLOP: case CAM_HORIZAREA: case CAM_PENCIL: return C3_CHK_MERGE;
    case CAM_WATERLINE: case CAM_RASTER: case CAM_SPIRAL: case CAM_RADIAL: case CAM_HSTPROJECT: {
        /* mutlGetHstCheckTrimmingMethod: the registry value, 1 when absent. MCAM_HST_CHECK_TRIM (testing) stands for that value: 0 = merge
           (FUN_180258310(2), as the styles above); anything else = the kernel trimming (refused). Question SS-8 (formats/TODO.md). */
        const char *e = getenv("MCAM_HST_CHECK_TRIM");
        return e && atoi(e) == 0 ? C3_CHK_MERGE : C3_CHK_KERNEL; }
    case CAM_SRPOCKET: case CAM_SRMPOCKET: return C3_CHK_249;
    default: return C3_CHK_CLASSIC;
    }
}
static int c3_check_refuse(int type, char *err, size_t errn) {
    int r = c3_check_rule(type);
    if (r == C3_CHK_249) snprintf(err, errn, "This operation cannot have check surfaces");   /* mcmsurfres 249 */
    else if (r == C3_CHK_KERNEL) snprintf(err, errn, "check surfaces: the HST kernel trims this style's passes to them (Edit Passes to Surfaces plan 0x141f, cam3d_checktrim.c) - not wired to this generator");
    else if (r == C3_CHK_CLASSIC) snprintf(err, errn, "check surfaces: this surface operation's check model (FUN_180290fb0's contact solvers) is NOT DECODED");
    return r != C3_CHK_MERGE;
}
/* The check trimming of the C3_CHK_KERNEL styles is ported (Edit Passes to Surfaces 0x141f, cam3d_checktrim.c, N13-10): c3_chk_open meshes
   the check references as their own model (want = TP_REF_CHECK below) and sets this flag for the next c3_mesh_op of the op, which then
   leaves them out of the drive (FUN_180258310(0, ..) writes the drive only, surface_selection.md 8.1). Reset by that c3_mesh_op. */
static _Thread_local int g_c3_chk_kernel;
static int c3_mesh_refs(const McamFile *f, CamOp *op, double defl, int want, C3Mesh *m, char *err, size_t errn);
static int c3_mesh_op(const McamFile *f, CamOp *op, double defl, C3Mesh *m, char *err, size_t errn) { return c3_mesh_refs(f, op, defl, TP_REF_DRIVE, m, err, errn); }
/* want = TP_REF_DRIVE: the drive model (with the checks by c3_check_rule); TP_REF_CHECK: the check references alone (none: m->n = 0, no error) */
static int c3_mesh_refs(const McamFile *f, CamOp *op, double defl, int want, C3Mesh *m, char *err, size_t errn) {
    memset(m, 0, sizeof *m);
    int chk_kernel = g_c3_chk_kernel; if (want == TP_REF_DRIVE) g_c3_chk_kernel = 0;
    if (!g_c3_stl && getenv("MCAM_3D_STL")) g_c3_stl = getenv("MCAM_3D_STL");   /* ours (testing): the drive from an STL (the app's scenes; cam_cli --stl) */
    if (g_c3_stl) {
        if (want == TP_REF_CHECK) return 1;   /* the testing STL drive has no checks */
        m->n = cam3d_stl_load(g_c3_stl, &m->t);
        if (m->n <= 0) { snprintf(err, errn, "cannot read the STL %s", g_c3_stl); return 0; }
        c3_mesh_box(m); return 1;
    }
    /* the drive (formats/surface_selection.md 2.2): the op's references - its own list, else (a stored op, file_op_id) its TP kind 4
       records (cam_refs_from_tp; [measured] TUTORIAL 7 METRIC ops 5 / 6: kind 4 records naming the drive NURBS surfaces, the containment
       as kind 6 / 7). Drive = tp_ref_role (msurf_ent_count l.16410-16418: no check 0x4 / gouge 0x20 flag). Check references: by the
       op type's rule (c3_check_rule, S7): meshed with the drive (HST styles 0 / 1 / 3 / 4 / 6, with Mastercam's warning 282 / 287 in
       op->warning) or refused. Gouge references (0x20, multiaxis; none in the corpus' 3D ops): their use by a 3D op is NOT DECODED,
       refused. Solid records: the role is the SldToolPOp's (c3_solid_role, 2.3 / section 7: whole bodies by their body type, single
       face picks refused, N13-7; every solid reference of the corpus' 3D ops names a whole body). */
    const CamRef *R = op->ref; int nr = op->nref; CamRef *own = NULL;
    if (!nr && op->p.file_op_id > 0) {
        own = malloc(sizeof(CamRef) * CAM_REF_MAX);
        nr = cam_refs_from_tp(f, op->p.file_op_id, own, CAM_REF_MAX, err, errn);
        if (nr < 0) { free(own); return 0; }
        R = own;
    }
    int ndrv = 0, nchk = 0; unsigned char *role = malloc((size_t)nr + 1);
    for (int d = 0; d < nr; d++) {
        role[d] = (unsigned char)tp_ref_role(R[d].flags);
        if (role[d] == TP_REF_FLAG1) { snprintf(err, errn, "a reference with flag 0x1 without 0x2 (msurf_ent_count: not a drive; its meaning NOT DECODED)"); free(role); free(own); return 0; }
        if (role[d] == TP_REF_DRIVE && R[d].cls == TP_ECLS_SOLID) {   /* msurf_ent_count l.16419-16437: a solid's faces by their own types */
            int si = R[d].index; char why[200];
            if (si < 0 || si >= f->nsolids) { snprintf(err, errn, "drive solid %d is not in the part", si >= 0 ? si + 1 : R[d].idn); free(role); free(own); return 0; }
            int sr = c3_solid_role(&f->solids[si], R[d].link, why, sizeof why);
            if (sr < 0) { snprintf(err, errn, "solid %d: %s", si + 1, why); free(role); free(own); return 0; }
            role[d] = (unsigned char)sr;
        }
        if (role[d] == TP_REF_GOUGE) { snprintf(err, errn, "a gouge reference (flag 0x20, multiaxis): its use by a 3D surface operation is NOT DECODED"); free(role); free(own); return 0; }
        if (role[d] == TP_REF_DRIVE) ndrv++;
        if (role[d] == TP_REF_CHECK) nchk++;
    }
    if (want == TP_REF_CHECK) { if (!nchk) { free(role); free(own); return 1; } goto mesh; }
    /* bnci_surface l.12272-12278: no drive (none, or every selected surface is a check) -> mcmsurfres 241 (0xf1), no toolpath */
    if (ndrv <= 0) { snprintf(err, errn, nchk ? "Surface operation does not contain any drive surfaces" : "no drive solid or surface selected"); free(role); free(own); return 0; }
    if (nchk && !(chk_kernel && c3_check_rule(op->p.type) == C3_CHK_KERNEL) && c3_check_refuse(op->p.type, err, errn)) { free(role); free(own); return 0; }
    if (nchk && !chk_kernel) {   /* C3_CHK_MERGE: the checks join the drive model (FUN_180258310(2, ..)); FUN_1800202d0's warning */
        int t = op->p.type;   /* FUN_1800202d0: 282 for styles 0 / 1, 287 for 3 / 4 / 6; none for the method-0 styles (l.23118: style - 3 < 4 and != 5) */
        if (t == CAM_AREAROUGH || t == CAM_OPTIROUGH) snprintf(op->warning, sizeof op->warning, "Check geometry in HST/Roughing operation will be treated as drive geometry");   /* mcmsurfres 282 */
        else if (t == CAM_SCALLOP || t == CAM_HORIZAREA || t == CAM_PENCIL) snprintf(op->warning, sizeof op->warning, "Check geometry in this HST/Finishing operation will be treated as drive geometry");   /* mcmsurfres 287 */
        for (int d = 0; d < nr; d++) if (role[d] == TP_REF_CHECK) role[d] = TP_REF_DRIVE;
    }
  mesh:;
    const char *rw = want == TP_REF_CHECK ? "check" : "drive";
    /* XT is in metres (formats/xt.md); the part's units: metric (f->metric 1, or unknown) mm, inch 1000 / 25.4 */
    double scale = !mcam_is_metric(f) ? 1000.0 / 25.4 : 1000.0;
    int nsurf = 0;
    /* The HST drive mesh behind MCAM_HST_MESH=1 (N13-30 / N13-33 / N13-34 / N13-35; formats/hst_linker.md 27 / 33). User decision N13-36 (2026-11-14)
       turns it on by default; that flip is prepared below but left OFF until its regress3 run is complete and explained (hst_linker.md 33). An HST op's drive surfaces (kinds
       0, 1, 8 and 10 over them) meshed as Mastercam meshes them for mchmm (mcmsurf FUN_180258310 on make_surf_model + fill_mach_model_buffer,
       cam3d_mmodel.c mm_hst_surface) instead of mcs_mesh; the values from the op record: msurf_chord_dev = record 0x12a0 (msurf_map l.340012),
       style 0x16bc, stock 0x13c0 / 0x13c8 (FUN_180258310 l.395652-395661). Solids: Parasolid's faceting (make_stl_model_from_sld_face ->
       ps_face_to_mach_model -> tmpstlc.tmp, FUN_180229ba0) - third-party, NOT DECODED: such a drive keeps the old mesh (N13-36, below); polygon meshes (makePmeshStlModel) NOT PORTED.
       The kernel-trimming check model (want = TP_REF_CHECK, cam3d_checktrim.c) is mckernel's tmpmn5.emm (FUN_18006ea44) = mcmsurf
       FUN_180220fe0's tmpmn5.tmp machine model converted by mckernel FUN_1800f200c and renamed (hst_linker.md 21.5) - NOT PORTED: it keeps
       mcs_mesh. */
    const char *hme = getenv("MCAM_HST_MESH"); int hmv = hme ? atoi(hme) : 0;   /* N13-36 decided "on", but the flip is NOT verified yet (its regress3 run was not finished, 2026-11-14): unset = off until it is; MCAM_HST_MESH=1 turns it on */
    int hstm = want == TP_REF_DRIVE && hmv && (cam_hst_style(op->p.type) >= 0 || op->p.type == CAM_OPTIROUGH);
    /* N13-36 (user decision, a deviation the user accepted): an op whose drive holds a solid keeps TODAY's mesh for the whole drive (OCCT for the
       solids, mcs_mesh for its surfaces). Mastercam meshes a solid for mchmm with Parasolid's faceter (make_stl_model_from_sld_face ->
       PK_TOPOL_render_facet, mcmsurf FUN_180229ba0 ms l.363767), a third-party kernel the port cannot run (NOT DECODED, hst_linker.md 27.4), so
       such a drive is NOT Mastercam's mesh; the whole drive keeps the old mesh rather than mixing the two (no mixed mesh, cf. the =2 diagnostic). */
    if (hstm) for (int d = 0; d < nr; d++) if (role[d] == want && R[d].cls == TP_ECLS_SOLID) { hstm = 0; break; }
    double h_chord = 0, h_sxy = 0, h_sz = 0, h_zlo = 0, h_zhi = 0; int h_style4 = 0, h_first = 1, h_ids = 1; unsigned short h_id = 0, h_attr = 0;
    if (hstm) {
        CamParams q = op->p; uint32_t hrn = 0; uint8_t *hr = hst_record(f, &q, NULL, &hrn);
        if (!hr || hrn < 0x16c0) { free(hr); snprintf(err, errn, "HST drive mesh: no op record"); free(role); free(own); return 0; }
        memcpy(&h_chord, hr + 0x12a0, 8); memcpy(&h_sxy, hr + 0x13c0, 8); memcpy(&h_sz, hr + 0x13c8, 8); h_style4 = hr[0x16bc] == 4;
        /* the frame's view (formats/hst_linker.md 21.5): GetActiveViewNo(Planes, 0) = the active Cplane, which bnci_surface (mcmsurf l.11916)
           sets from the op's Cplane op_view (op +0x620 = record 0x688: +8 the matrix, +0x68 the plane id) through ActivePlaneSaveRestore;
           msurf_map (l.339996-340000) takes plane 1 (Top) when its view number is 0. The port's HST engine works in Top: another Cplane is
           refused (no rotated drive mesh is consumed downstream) */
        /* process_3rd_party_algorithm FUN_1800199d0 (mcmsurf l.18200-18230): with op +0x15fa (record 0x1662) != 0 the drive STL may come
           from the interfacial model (ConvertBinIFFToStlFile, when FUN_1803b2750 / FUN_1803accc0 hold) instead of FUN_180258310 */
        { int16_t iff; memcpy(&iff, hr + 0x1662, 2);
          if (iff) { free(hr); snprintf(err, errn, "HST drive mesh: record 0x1662 set - the IFF drive path (ConvertBinIFFToStlFile, FUN_1800199d0) NOT DECODED"); free(role); free(own); return 0; } }
        { int16_t pid; double V[9]; memcpy(&pid, hr + 0x6f0, 2); memcpy(V, hr + 0x690, sizeof V);
          static const double TOP[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
          if (pid != 0 && memcmp(V, TOP, sizeof V)) {
              free(hr); snprintf(err, errn, "HST drive mesh in a Cplane other than Top (plane %d): FUN_180258310 writes the mesh in that view's frame, which the port's HST engine does not take - NOT PORTED", pid);
              free(role); free(own); return 0;
          } }
        /* FUN_180258310 l.395613-395637: style 5 (op +0x1654) with a contact angle (op +0x11e0 / +0x11e8 = record 0x1248 / 0x1250) strictly inside
           (tol, pi / 2 - tol) writes no surface ids (local_2fa6 = 0); tol = the system tolerance */
        if (hr[0x16bc] == 5) {
            double a1, a2, st = mc_systol_units(mcam_is_metric(f))->tol; memcpy(&a1, hr + 0x1248, 8); memcpy(&a2, hr + 0x1250, 8);
            double hi = a2 < a1 ? a1 : a2, lo = a2 <= a1 ? a2 : a1;
            if ((st < hi && hi < 1.570796326794896 - st) || (st < lo && lo < 1.570796326794896 - st)) h_ids = 0;
        }
        free(hr);
    }
    for (int d = 0; d < nr; d++) {
        const CamRef *r = &R[d];
        if (role[d] != want) continue;
        double *t = NULL; char e[200] = ""; int nt;
        if (hstm && r->cls == TP_ECLS_SURF) {
            int sk = r->index; const char *w = NULL;
            if (sk < 0 || sk >= f->nsurfs) { snprintf(err, errn, "%s surface %d is not in the part", rw, sk >= 0 ? sk + 1 : r->idn); c3_mesh_free(m); free(role); free(own); return 0; }
            nt = mm_hst_surface(f, &f->surfs[sk], h_chord, mcam_is_metric(f), h_style4, h_sxy, h_sz, &t, NULL, &h_zlo, &h_zhi, &h_first, &w);   /* Top (checked above) */
            if (nt < 0 && hmv == 2) {   /* diagnostic only (ours): a surface the port refuses keeps mcs_mesh - a mixed mesh, not Mastercam's */
                fprintf(stderr, "MCAM_HST_MESH=2: surface %d (kind %d) keeps mcs_mesh: %s\n", sk + 1, f->surfs[sk].kind, w ? w : "?");
                nt = mcs_mesh(f, &f->surfs[sk], defl, &t, NULL, NULL, e, sizeof e);
                if (nt >= 0) for (int i = 0; i < 3 * nt; i++) { double z = t[3 * i + 2]; if (h_first) { h_zlo = h_zhi = z; h_first = 0; } if (z < h_zlo) h_zlo = z; if (z > h_zhi) h_zhi = z; }
            }
            if (nt < 0) { snprintf(err, errn, "surface %d: %s", sk + 1, w ? w : "HST drive mesh refused"); c3_mesh_free(m); free(role); free(own); return 0; }
            /* the attribute (l.395948-395957): at a surface's first triangle, with ids, the counter + 1 (0x7fff wraps to 0 first) | 0x8000 */
            if (nt > 0 && h_ids) { if (h_id == 0x7fff) h_id = 0; h_id++; h_attr = (unsigned short)(h_id | 0x8000); }
            m->attr = realloc(m->attr, sizeof(unsigned short) * (m->n + nt + 1));
            for (int i = 0; i < nt; i++) m->attr[m->n + i] = h_attr;
            nsurf++;
        } else if (r->cls == TP_ECLS_SOLID) {
            int si = r->index;
            if (si < 0 || si >= f->nsolids) { snprintf(err, errn, "%s solid %d is not in the part", rw, si >= 0 ? si + 1 : r->idn); c3_mesh_free(m); free(role); free(own); return 0; }
            nt = f->solids[si].brep ? occt_brep_drive(f->solids[si].brep, f->solids[si].brep_n, defl, &t, e, sizeof e)   /* an imported solid: model units */
                                    : occt_mesh_xt(f->solids[si].xt, f->solids[si].xt_size, scale, defl, &t, e, sizeof e);
            if (nt < 0) { snprintf(err, errn, "solid %d: %s", si + 1, e); c3_mesh_free(m); free(role); free(own); return 0; }
        } else if (r->cls == TP_ECLS_SURF) {   /* surfaces are in model units */
            int sk = r->index;
            if (sk < 0 || sk >= f->nsurfs) { snprintf(err, errn, "%s surface %d is not in the part", rw, sk >= 0 ? sk + 1 : r->idn); c3_mesh_free(m); free(role); free(own); return 0; }
            nt = mcs_mesh(f, &f->surfs[sk], defl, &t, NULL, NULL, e, sizeof e);
            if (nt < 0) { snprintf(err, errn, "surface %d: %s", sk + 1, e); c3_mesh_free(m); free(role); free(own); return 0; }
            { const char *dp = getenv("MCAM_3D_DUMPMESH");   /* ours (testing): "s INDEX ENTITY" + one line per triangle "x y z x y z x y z" */
              if (dp) { FILE *fp = fopen(dp, nsurf ? "a" : "w"); if (fp) { fprintf(fp, "s %d %d\n", sk, f->surfs[sk].idn);
                  for (int i = 0; i < nt; i++) { for (int k = 0; k < 9; k++) fprintf(fp, "%.6f ", t[9 * i + k]); fputc('\n', fp); } fclose(fp); } } }
            nsurf++;
        } else {
            snprintf(err, errn, "a %s %s reference is NOT PORTED", rw, r->cls == TP_ECLS_MESH ? "mesh (PMESH_ID)" : r->cls == TP_ECLS_FILE ? "CAD file" : "entity of an unknown class");
            c3_mesh_free(m); free(role); free(own); return 0;
        }
        m->t = realloc(m->t, sizeof(double) * 9 * (m->n + nt + 1));
        memcpy(m->t + 9 * m->n, t, sizeof(double) * 9 * nt); m->n += nt; free(t);
    }
    free(role); free(own);
    if (m->n <= 0) { if (want == TP_REF_CHECK) return 1; snprintf(err, errn, "the drive gave no triangles"); return 0; }
    c3_mesh_box(m);
    if (hstm && !h_first) { m->lo[2] = h_zlo; m->hi[2] = h_zhi; }   /* hstJobIdDat +0x28 / +0x30: FUN_180258310's vertex range before the float STL */
    return 1;
}

/* --------------------------------------------------------------------------------------------- the op values */
/* record offsets of op 132 (the HST page fields; mcmsurf's local op = record + 0x2088, e.g. stepdown local +0x32b0 =
   record 0x1228, tool diameter +0x23d8 = 0x350). Sources: HSTOptRoughParamPage UpdateLocalOp FUN_180093c30 /
   MapToDlg FUN_180091f20 + FUN_180091cd0 (decomp/mcmsurf_all.c l.96635 / 95644 / 95536), HSTOptRoughTransitionsPage
   (dialog 1430, formats/ui_fields_mcmsurf.tsv entity offsets), the record fields TPOptRoughToolPath reads through the
   block copies TP = record - 0x1078 / 0x1088 / 0x10b8 / 0x1269 (anchors: stepdown TP+0x1b0 = 0x1228, helix TP+0x2a0 =
   0x1328, step up TP+0x3c0 = 0x1478, StepUpEnable TP+0x483 = 0x16ec, retract mode TP+0x482 = 0x16eb; the copy itself is
   mccore's, NOT DECODED - the anchors are consistent across each block). */
typedef struct {
    double D, Rc;               /* 0x350 tool diameter, 0x358 corner radius (FUN_180091cd0: flat diameter = D - 2 Rc) */
    double stepdown, stepover;  /* 0x1228 (TP+0x1b0 Stepdown), 0x1230 (TP+0x1b8: MinOffset / MaxOffset, engagement) */
    int stepup_on; double stepup;   /* 0x16ec (TP+0x483 StepUpEnable), 0x1478 (TP+0x3c0 StepUp) */
    double min_rad;             /* 0x1480 (TP+0x3c8 -> CDynPockParm +0x40) */
    double micro_lift, back_feed;   /* 0x1488 (TP+0x3d0 -> +0x48), 0x1490 */
    double conv_feed;           /* 0x1508 */
    int gap_pct; double gap_d, gap_p;   /* radio 0x16cd (1 = %), 0x1308 (TP+0x280 -> +0xe0), 0x1310 % of D */
    int retract_mode;           /* 0x16eb (TP+0x482 -> flags 0x10000 / 0x40000 / 0x80000 / 0x20000) */
    int conventional, zigzag;   /* cut method: 0x16e4 (K+0x47c) / 0x1728 (K+0x487): UpdateLocalOp l.96660-96676; K = TPHSTToolPath offset as the kernel reads it, the record <-> K map = the mccore copy FUN_180923f60 (decomp/proj/d4.c l.60-255) */
    int16_t stepdown_order;     /* 0x1660 (K+0x4ea, s16: FUN_180923f60 d4.c l.240 TP m 0x54a = op 0x15f8): CDynPockParm +0x20 (FUN_180108f1c mk l.205385-205391) */
    int core;                   /* 0x16ef (TP+0x486: 0x40 flag, approach 0.65 D) */
    int b485, b480, b481, entry; /* 0x16ee / 0x16e9 / 0x16ea / 0x16e8 (K+0x485 / 480 / 481 / 47f) */
    double helix_r, zclear, plunge, pitch, troch_r, slot_w, ramp_feed, skip_small;   /* 0x1328 0x1330 0x1338 0x1460 0x1450 0x1458 0x1468 0x13e8 */
    double tol;                 /* StepMachTolerance (plan 4.2): see c3_values */
    double sxy, sz;             /* stock to leave on walls / floors (ToolThicknessXY / Z) */
    double ztol;                /* 0x12a0 = op +0x1238: the drive's chordal deviation msurf_chord_dev (msurf_map, decomp/mcmsurf_all.c
                                   l.340012, op 132) - the tessellation tolerance here; TP+0x220 [inferred]: widens PosZHi / PosZLo */
    int clamped;                /* the stepover was cut to 95 % of the flat diameter */
    /* tool containment (mcmsurf dialog 25624, DDX FUN_18039af70, MapToDlg FUN_18039b4d0 decomp/mcmsurf_all.c l.573978: local op
       +0x374f / +0x3338 / +0x3750 / +0x37b8 = record 0x16c7 / 0x12b0 / 0x16c8 / 0x1730) */
    int cont_pos;               /* 0x16c7: radio 1217 Inside (0) / 1218 Center (1) / 1219 Outside (2) */
    double cont_off;            /* 0x12b0 Offset distance */
    int cont_add_r;             /* 0x16c8 Add offset distance to tool radius */
} C3Val;
static double c3d(const uint8_t *r, uint32_t n, uint32_t off) { double v = 0; if (off + 8 <= n) memcpy(&v, r + off, 8); return v; }
/* StepMachTolerance = job +0xa0 (param_1[0x14]) = 0.5 x TP+0x220 (record 0x12a0), or 0.5 x 0.025 / 0.001 (metric / inch) when TP+0x220 <= 1e-12: FUN_18006dd68 mk
   l.84701-84718 (every HST tool path: OptRoughToolPath, HybridWSToolPath, ProjectToolPath use this constructor); passed as "StepMachTolerance" at mk l.87612 (+ local_848, set only
   for the Pencil style 5). The other uses of TP+0x220 (0.5 x above PosZ, 2 x the Bounding Box offset) are unchanged. */
static double c3_step_tol(const uint8_t *r, uint32_t n, int metric) {
    double t = c3d(r, n, 0x12a0);
    if (!(t > 1e-12)) t = metric ? 0.025 : 0.001;
    return t * 0.5;
}
static int c3b(const uint8_t *r, uint32_t n, uint32_t off) { return off < n ? r[off] : 0; }
static void c3_values(const uint8_t *r, uint32_t n, const CamParams *p, int metric, C3Val *v) {
    memset(v, 0, sizeof *v);
    v->D = p->tool_dia > 0 ? p->tool_dia : c3d(r, n, 0x350);
    v->Rc = p->tool_dia > 0 ? p->corner_r : c3d(r, n, 0x358);
    v->stepdown = c3d(r, n, 0x1228); v->stepover = c3d(r, n, 0x1230);
    {   /* FUN_180091cd0 (decomp/mcmsurf_all.c l.95536, the page's stepover check): at most 95 % of the flat diameter D - 2 Rc (of D when
           that is below FLT_EPSILON), with the warning mcmsurfres 500 / 501 */
        double fd = v->D - 2 * v->Rc; if (!(fd >= 1.1920928955078125e-07)) fd = v->D;
        if (v->stepover > fd * 0.95) { v->stepover = fd * 0.95; v->clamped = 1; }
    }
    v->stepup_on = c3b(r, n, 0x16ec); v->stepup = c3d(r, n, 0x1478);
    v->min_rad = c3d(r, n, 0x1480); v->micro_lift = c3d(r, n, 0x1488); v->back_feed = c3d(r, n, 0x1490);
    v->conv_feed = c3d(r, n, 0x1508);
    v->gap_pct = c3b(r, n, 0x16cd) == 1;   /* radio 1473 Distance (0) / 1478 % of tool diameter (1): the defaults hold 1 with 0x1308 = 5 x D = 0x1310 % */
    v->gap_d = c3d(r, n, 0x1308); v->gap_p = c3d(r, n, 0x1310);
    v->retract_mode = c3b(r, n, 0x16eb);
    v->conventional = c3b(r, n, 0x16e4); v->zigzag = c3b(r, n, 0x1728);
    { int16_t o = 0; if (0x1660 + 2 <= n) memcpy(&o, r + 0x1660, 2); v->stepdown_order = o; }
    v->core = c3b(r, n, 0x16ef);
    v->cont_pos = c3b(r, n, 0x16c7); v->cont_off = c3d(r, n, 0x12b0); v->cont_add_r = c3b(r, n, 0x16c8);
    v->b485 = c3b(r, n, 0x16ee); v->b480 = c3b(r, n, 0x16e9); v->b481 = c3b(r, n, 0x16ea); v->entry = c3b(r, n, 0x16e8);
    v->helix_r = c3d(r, n, 0x1328); v->zclear = c3d(r, n, 0x1330); v->plunge = c3d(r, n, 0x1338); v->pitch = c3d(r, n, 0x1460);
    v->troch_r = c3d(r, n, 0x1450); v->slot_w = c3d(r, n, 0x1458); v->ramp_feed = c3d(r, n, 0x1468); v->skip_small = c3d(r, n, 0x13e8);
    /* StepMachTolerance = job +0xa0 (+ 0 for OptiRough: local_848 is only set for style 5, mk l.87420-87428, l.87612) = 0.5 x record 0x12a0 (c3_step_tol) */
    v->tol = c3_step_tol(r, n, metric);
    /* stock to leave: ToolThicknessXY = TP+0x318, ToolThicknessZ = TP+0x320 (the macro builder FUN_180070b1c, decomp/mckernel_all.c
       l.86744-86770; FUN_18006f2e8 / FUN_18006f3a4 read the same two). Their record offsets 0x13c0 / 0x13c8 (block shift TP = record
       - 0x10a8) are [inferred] from data: walls / floors 1 / 1 in every op 132 of Mill_MM.mcam-defaults, 8 / 1 in the TUTORIAL 7
       OptiRough, 4 / 0 in its Hybrid finish, 0.05 / 0.05 in MOLD BOTTLE's roughing and 0 / 0 in its scallop finish; with them the
       TUTORIAL 7 oracle cuts Mastercam's region (formats/hst3d_port_plan.md 6.6). The page that edits them (Model Geometry) and the
       adjustment of FUN_180070b1c's bVar3 branch (one stock reduced by the other for a tool whose corner radius sum is negative) are
       NOT PORTED. */
    v->sxy = c3d(r, n, 0x13c0); v->sz = c3d(r, n, 0x13c8);
    v->ztol = c3d(r, n, 0x12a0);
    (void)p;
}

/* --------------------------------------------------------------------------------------------- Z levels */
/* EvaZProfilePlan next level FUN_18040da70 (hmm_zplan.c l.13173): the first call returns PosZHi (+0x2b9 flag), then
   r = (z - PosZLo) / Stepdown; n = 1 if r <= StepDownInflexionTolerance else ceil(r - StepDownInflexionTolerance);
   n > StepDownEvenLastSteps (OptiRough: 1, mk l.88404) and n != 1: z - Stepdown; otherwise
   (1 - 1/n) z + (1/n) PosZLo with 1/n in single precision as decompiled ((double)(1.0 / (float)n)), n = 1 ends.
   StepDownInflexionTolerance = "min(0.01, Stepdown * 0.01)" for the plans other than 0x143a (hmm_pass0.c l.536-570). */
/* even = StepDownEvenLastSteps (OptiRough 1, mk l.88404; Waterline 5, the plan 0x1436 default hmm_pass0.c l.596-601) */


/* --------------------------------------------------------------------------------------------- the slice ("weave") */
/* MachWeaveResolution (FUN_180310790 hmm_pass0.c l.723-799, plan 4.5):
     WeaveFlatTip = (ToolCornerRadius + ToolThicknessZ <= 0); VorProfileFlatRad = WeaveFlatTip ? 0 : max(0, R - Rc);
     WeaveCutterRadius = R + ThickZ - VorProfileFlatRad;
     MachWeaveResolution = 1.414 * sqrt(sq(WeaveCutterRadius) - sq(max(WeaveCutterRadius - StepMachTolerance, 0))) */
static double c3_resolution(double R, double Rc, double sz, double tol) {
    int flat = Rc + sz <= 0.0;
    double fr = flat ? 0.0 : fmax(0.0, R - Rc), wr = R + sz - fr, a = fmax(wr - tol, 0.0);
    return 1.414 * sqrt(wr * wr - a * a);
}

/* w0 / w1: the contact words of the two ends (Waterline's weave, c3_ct below; 0 elsewhere) */
typedef struct { double lo, hi; unsigned w0, w1; } C3Iv;
typedef struct { C3Iv *iv; int n, cap; } C3Fib;
static void fib_addw(C3Fib *f, double lo, double hi, unsigned w0, unsigned w1) {
    if (f->n == f->cap) { f->cap = f->cap ? 2 * f->cap : 8; f->iv = realloc(f->iv, sizeof(C3Iv) * f->cap); }
    f->iv[f->n].lo = lo; f->iv[f->n].hi = hi; f->iv[f->n].w0 = w0; f->iv[f->n].w1 = w1; f->n++;
}
static void fib_add(C3Fib *f, double lo, double hi) { fib_addw(f, lo, hi, 0, 0); }
static int iv_cmp(const void *a, const void *b) { double d = ((const C3Iv *)a)->lo - ((const C3Iv *)b)->lo; return d < 0 ? -1 : d > 0; }
static void fib_merge(C3Fib *f) {     /* the fibre's interval set is the union of the per-triangle intervals (plan 4.5 step 6) */
    if (f->n < 2) return;
    qsort(f->iv, f->n, sizeof(C3Iv), iv_cmp);
    int k = 0;
    for (int i = 1; i < f->n; i++) { if (f->iv[i].lo <= f->iv[k].hi) { if (f->iv[i].hi > f->iv[k].hi) f->iv[k].hi = f->iv[i].hi; } else f->iv[++k] = f->iv[i]; }
    f->n = k + 1;
}

/* CZInflexions (mchmm.dll, ctor FUN_1804a1b80 / triangle FUN_1804a2270 / edge FUN_1804a20b0 / vertex FUN_1804a2040 / query FUN_1804a1e10): a level is moved off the heights of the
   model's vertices, flat edges and near-horizontal facets. Used for every Z-profile level: FUN_18040cf60 (hz l.12945-12975, Waterline / Area) and FUN_180413d90 (hz l.16565-16590, OptiRough);
   StepDownInflexionToleranceEnable 0x1df6 defaults to "true" (hmm_pass0.c l.517-521, not set by mckernel). Inputs: z range (nominal, previous) - c, c = Eva +0x140 = ToolThicknessZ
   (FUN_1802f4c80 reads 0x1d20); Avoidance 0x1bbf = 0.5 x Tolerance 0x1bbe (hp l.533-577; OptiRough's plan keeps it, the 0.0001 at mk l.205595 is the Stock Model macro's); Gradient 0x1bbd default 2e-5 (hmm_pass0.c l.525). The XY box is the
   weave's (ours: here the containment box). Ours: the triangles are our tessellation's, so a curved surface's vertex heights are not Mastercam's; flats are. */
typedef struct { double *z; int n, cap; C3Iv *iv; int ni, ci; double lo, hi, g, bx[4]; } C3Infl;
static void ci_z(C3Infl *c, double z) { if (c->n == c->cap) { c->cap = c->cap ? 2 * c->cap : 64; c->z = realloc(c->z, sizeof(double) * c->cap); } c->z[c->n++] = z; }
static void ci_iv(C3Infl *c, double a, double b) {   /* FUN_1804a1fd0: clamped to the z range, dropped when empty */
    if (a < c->lo) a = c->lo;
    if (c->hi < b) b = c->hi;
    if (b <= a && a != b) return;
    if (c->ni == c->ci) { c->ci = c->ci ? 2 * c->ci : 16; c->iv = realloc(c->iv, sizeof(C3Iv) * c->ci); }
    c->iv[c->ni].lo = a; c->iv[c->ni].hi = b; c->ni++;
}
static void ci_vertex(C3Infl *c, const double *P) {   /* FUN_1804a2040 */
    if (c->bx[0] <= P[0] && P[0] <= c->bx[2] && c->bx[1] <= P[1] && P[1] <= c->bx[3] && c->lo <= P[2] && P[2] <= c->hi) ci_z(c, P[2]);
}
/* FUN_1804a20b0 (mchmm.dll 1804a20b0): an edge whose XYZ box meets the collector's box and Z window: equal end heights (FUN_1800082c0, exact) -> that
   height into the set (FUN_1804a2630 on +0x70); else a near-flat edge (dz^2 <= (dx^2 + dy^2 + dz^2) g^2, g = +0x50) -> its [z1, z2] (FUN_1804a1fd0) */
static void ci_edge(C3Infl *c, const double *P, const double *Q) {
    if (fmax(P[0], Q[0]) < c->bx[0] || c->bx[2] < fmin(P[0], Q[0]) || fmax(P[1], Q[1]) < c->bx[1] || c->bx[3] < fmin(P[1], Q[1]) || fmax(P[2], Q[2]) < c->lo || c->hi < fmin(P[2], Q[2])) return;
    if (P[2] == Q[2]) { ci_z(c, P[2]); return; }
    double dx = Q[0] - P[0], dy = Q[1] - P[1], dz = Q[2] - P[2];
    if (dz * dz <= (dy * dy + dx * dx + dz * dz) * c->g * c->g) ci_iv(c, fmin(P[2], Q[2]), fmax(P[2], Q[2]));
}
static void ci_tri(C3Infl *c, const double *A, const double *B, const double *C) {   /* FUN_1804a2270 */
    double zl = fmin(A[2], fmin(B[2], C[2])), zh = fmax(A[2], fmax(B[2], C[2]));
    if (fmax(A[0], fmax(B[0], C[0])) < c->bx[0] || c->bx[2] < fmin(A[0], fmin(B[0], C[0])) || fmax(A[1], fmax(B[1], C[1])) < c->bx[1] || c->bx[3] < fmin(A[1], fmin(B[1], C[1])) || zh < c->lo || c->hi < zl) return;
    if (A[2] == B[2] && A[2] == C[2]) { ci_vertex(c, A); ci_iv(c, A[2], A[2]); return; }
    double e0 = B[0] - A[0], e1 = B[1] - A[1], e2 = B[2] - A[2], f0 = C[0] - A[0], f1 = C[1] - A[1], f2 = C[2] - A[2];
    double nz = e0 * f1 - e1 * f0, ny = e0 * f2 - e2 * f0, nx = e1 * f2 - e2 * f1, h2 = ny * ny + nx * nx;
    if (c->g > 0 && h2 <= (nz * nz + h2) * c->g * c->g) ci_iv(c, zl, zh);
    /* N13-30 (2026-11-14, extended to OptiRough / Area Roughing the same day): FUN_1804a2270's tail as decoded (Ghidra DecompAt of mchmm.dll,
       hst_linker.md 21.1): the three edges (FUN_1804a20b0), then the three vertices (FUN_1804a2040) - on our tessellation these heights are not
       Mastercam's, so the levels move (up to 0.009 mm on SWARF_METRIC op 3) until the drive mesh is mckernel's (a separate task) */
    ci_edge(c, A, B); ci_edge(c, B, C); ci_edge(c, C, A);
    ci_vertex(c, A); ci_vertex(c, B); ci_vertex(c, C);
}
static int dbl_cmp(const void *a, const void *b) { double d = *(const double *)a - *(const double *)b; return d < 0 ? -1 : d > 0; }
static int z_lb(const double *p, int n, double v) { int i = 0; while (i < n && p[i] < v) i++; return i; }       /* FUN_180060400 */
static double z_avoid(const double *p, int n, double x, double t, double up) {   /* FUN_18005f740 */
    for (int i = z_lb(p, n, x - t); i < n; i++) { if (x + t <= p[i]) return x; x = p[i] + t; if (up <= x) return up; }
    return x;
}
static double iv_avoid(const C3Iv *v, int n, double x, double t, double up) {    /* FUN_18005ff50 (lower bound on hi: FUN_18005fed0) */
    int i = 0; while (i < n && v[i].hi < x - t) i++;
    for (; i < n; i++) { if (x + t <= v[i].lo) return x; x = v[i].hi + t; if (up <= x) return up; }
    return x;
}
static double z_gap(const double *p, int n, double a, double b) {                /* FUN_18005f7e0: the point of [a, b] farthest from the set */
    if (n && a < p[n - 1] && p[0] < b) {
        int i0 = p[0] < a ? z_lb(p, n, a) : 0, i1 = b < p[n - 1] ? z_lb(p, n, b) : n;
        double da = 1e300, db = 1e300;
        if (i0 < n) da = fabs(a - p[i0]);
        if (i0 > 0) da = fmin(da, fabs(a - p[i0 - 1]));
        if (i1 < n) db = fabs(b - p[i1]);
        if (i1 > 0) db = fmin(db, fabs(b - p[i1 - 1]));
        double val = a, d = da;
        if (da < db) { d = db; val = b; }
        if (i0 == 0) i0 = 1;
        if (i1 == n) i1 = n - 1;
        for (int i = i0; i <= i1 && i < n; i++) { double g = (p[i] - p[i - 1]) * 0.5, m = (p[i] + p[i - 1]) * 0.5; if (d < g && a < m && m < b) { d = g; val = m; } }
        return val;
    }
    return (a + b) * 0.5;
}
/* FUN_1804a1e10 + FUN_18005ef10: the level in [nom, up] clear of the set and the intervals by avoid; none clear: the farthest point */
static double c3_infl_level(const C3Mesh *M, const double *bx, double c, double avoid, double nom, double prev, double up) {
    C3Infl I; memset(&I, 0, sizeof I);
    double lo = nom - c, hi = prev - c;
    if (avoid > 0) { I.lo = lo - avoid; I.hi = hi + avoid; ci_z(&I, I.lo); ci_z(&I, I.hi); ci_iv(&I, I.lo, I.lo); ci_iv(&I, I.hi, I.hi); }
    else { I.lo = lo; I.hi = hi; }
    I.g = 2e-5; memcpy(I.bx, bx, sizeof I.bx);
    for (int t = 0; t < M->n; t++) { const double *v = M->t + 9 * t; ci_tri(&I, v, v + 3, v + 6); }
    qsort(I.z, I.n, sizeof(double), dbl_cmp);
    { int k = 0; for (int i = 0; i < I.n; i++) if (!k || I.z[i] != I.z[k - 1]) I.z[k++] = I.z[i]; I.n = k; }
    if (I.ni) { C3Fib f = { I.iv, I.ni, I.ci }; fib_merge(&f); I.ni = f.n; }
    double a = nom - c, b = up - c, x = a, r;
    int use_set = 1, first = 1;
    for (;;) {                                                       /* FUN_18005ef10 */
        double u;
        if (use_set) { double x0 = x; x = z_avoid(I.z, I.n, x, avoid, b); u = x; if (x == b) break; if (!first && x == x0) { use_set = 0; continue; } }
        else { u = x; if (!first) break; }
        x = iv_avoid(I.iv, I.ni, u, avoid, b);
        if (x == b) break;
        use_set = x != u; first = 0;
    }
    r = x == b ? z_gap(I.z, I.n, a, b) : x;
    free(I.z); free(I.iv);
    return r + c;
}
/* the levels of EvaZProfilePlan with the inflexion rule (FUN_18040cf60 / FUN_180413d90): next nominal from the last level, then moved within + Tolerance (Bottom 0x1bc0 = 0.5 x it for the
   last level; the midpoint of the gap when the step is shorter). */
static int c3_levels_infl(const C3Mesh *M, const double *bx, double c, double zhi, double zlo, double sd, int even, double avoid, double unit, double **zp) {
    int k = 0, cap = 64; double *z = malloc(sizeof(double) * cap);
    *zp = z;
    if (!(sd > 1e-9) || zhi < zlo) return 0;
    /* unit: McHmm's constants are in its internal mm (FUN_1800edd8c scales the macro's lengths by job +0x80), so in an inch part the 0.01 cap is / 25.4 */
    double tol = fmin(0.01 * unit, sd * 0.01), bottom = tol * 0.5, av = avoid >= 0 ? avoid * unit : tol * 0.5, cur = zhi;
    z[k++] = cur;
    for (;;) {
        if (mcce_abort_get()) return -1;   /* cancelled */
        double r = (cur - zlo) / sd, nom, prev = cur; int n = r <= tol ? 1 : (int)ceil(r - tol), last = 0;
        if (n > even && n != 1) nom = cur - sd;
        else { if (cur == zlo) break; float inv = 1.0f / (float)n; nom = (1.0 - (double)inv) * cur + (double)inv * zlo; last = n == 1; }
        double t = last ? bottom : tol, up = nom + t;
        if (cur - t < nom + t) up = nom != cur ? (nom + cur) * 0.5 : nom;
        cur = c3_infl_level(M, bx, c, av, nom, cur, up);
        if (!(cur < prev)) return -2;   /* no descent (Stepdown below the precision of z): the decompile has no cap and would not end; refused, the caller's message */
        if (k == cap) { cap *= 2; z = realloc(z, sizeof(double) * cap); *zp = z; }
        z[k++] = cur;
        if (last) break;
    }
    return k;
}

/* the interval of t where the fibre point X(t) = (t, c, zc) is within distance R of a feature (ours: closed forms of the
   push-cutter tests, see the file head). lo / hi accumulate the min / max over the features hit. */
static _Thread_local int c3_weave_mc, c3_weave_resolve;   /* Waterline: the weave's loops by McHmm's traversal (wv_loops); WeaveResolveAmbiguous */
/* The contact word of an interval end (Waterline's weave, formats/hst_linker.md 31). McHmm's drop keeps, with each end of the interval being built, the
   contact that made it: point, normal, profile layer and a flag (the accumulator of FUN_180128800: lo +0x18 point / +0x48 normal / +0x78 layer / +0x80 flag,
   hi +0x30 / +0x60 / +0x7c / +0x81, hst3d_weave_drop.c), replaced together with the end when a feature moves it out (strictly: lo when lower, hi when higher,
   so the first feature wins a tie). The ball (profile type 3, FUN_180126130 vertex / FUN_180126e90 edge / FUN_180127d50 facet) records layer 0, flag 1 and
   the unit normal from the contact to the ball centre; a profile band k above it (FUN_180125dd0 / FUN_180126340 / FUN_1801275c0) layer k, flag 0 (or k - 1,
   flag 1 on the last band when the profile's +0x88 is set). FUN_180125b70 codes each end with FUN_180253420 (raster/tb2.c): word = (2 layer + flag) << 24
   (capped 0x7f) | q, q = round(t x 0xffffff) capped to [0, 0xffffff]; with the flag t = |n_xy| / (|n_xy| + n_z), else (layer 0) the contact point's
   horizontal distance / the profile radius, (layer k) its parameter along band k. An end clipped to the fibre window gets the window and | 0x80000000.
   c3_ct_on: push_tri computes the words of its interval's ends into c3_ct[0] / [1] (ours: the features are this file's closed forms of the same contacts,
   the ball below the centre and the cylinder above it, c3_ct_cyl; the cylinder's word is C3_CT_CYL - layer 1, flag 0: FUN_1802538d0 reads the normal
   of a band from the profile segment alone, horizontal for the cylinder, so its t is not needed by the filter and left 0). */
static _Thread_local int c3_ct_on, c3_ct_cyl;
static _Thread_local unsigned c3_ct[2];
#define C3_CT_CYL (2u << 24)
static unsigned c3_ct_ball(double nxy, double nz) {   /* FUN_180253420 with the flag set, layer 0 */
    double t = nxy / (nxy + nz); int q = (int)(t * 16777215.0 + 0.5);
    if (q > 0xfffffe) q = 0xffffff;
    if (!(q > 0)) q = 0;
    return (1u << 24) + (unsigned)q;
}
static void iv_take(double a, double b, unsigned wa, unsigned wb, double *lo, double *hi, int *hit) {
    if (a > b) return;
    if (!*hit || a < *lo) { *lo = a; c3_ct[0] = wa; }
    if (!*hit || b > *hi) { *hi = b; c3_ct[1] = wb; }
    *hit = 1;
}
/* the contact word of the ball centre X = (t, c, zc) touching the point Q (fibre frame; |n_xy| does not depend on the frame) */
static unsigned c3_ct_pt(double t, double c, double zc, const double *Q) {
    if (c3_ct_cyl) return C3_CT_CYL;
    return c3_ct_ball(hypot(t - Q[0], c - Q[1]), zc - Q[2]);
}
static void push_vertex(const double *P, double c, double zc, double R, double *lo, double *hi, int *hit) {
    double dv = c - P[1], dz = zc - P[2], s = R * R - dv * dv - dz * dz;
    if (s < 0) return;
    s = sqrt(s);
    unsigned wa = 0, wb = 0; if (c3_ct_on) { wa = c3_ct_pt(P[0] - s, c, zc, P); wb = c3_ct_pt(P[0] + s, c, zc, P); }
    iv_take(P[0] - s, P[0] + s, wa, wb, lo, hi, hit);
}
/* the contact word on the segment A B for the centre (t, c, zc): the segment's point nearest it */
static unsigned c3_ct_seg(double t, double c, double zc, const double *A, const double *B) {
    double d[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] }, dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    double s = dd > 0 ? ((t - A[0]) * d[0] + (c - A[1]) * d[1] + (zc - A[2]) * d[2]) / dd : 0.0;
    if (s < 0) s = 0;
    if (s > 1) s = 1;
    double Q[3] = { A[0] + s * d[0], A[1] + s * d[1], A[2] + s * d[2] };
    return c3_ct_pt(t, c, zc, Q);
}
static void push_edge(const double *A, const double *B, double c, double zc, double R, double *lo, double *hi, int *hit) {
    double du = B[0] - A[0], dv = B[1] - A[1], dz = B[2] - A[2], dd = du * du + dv * dv + dz * dz;
    if (dd < 1e-24) return;
    double q = (c - A[1]) * (c - A[1]) + (zc - A[2]) * (zc - A[2]), k = (c - A[1]) * dv + (zc - A[2]) * dz;
    double a = 1 - du * du / dd, b = -du * k / dd, c0 = q - k * k / dd - R * R;
    double t1, t2;   /* tau = t - Au */
    if (a > 1e-12) { double disc = b * b - a * c0; if (disc < 0) return; disc = sqrt(disc); t1 = (-b - disc) / a; t2 = (-b + disc) / a; }
    else { if (c0 >= 0) return; t1 = -1e300; t2 = 1e300; }
    if (fabs(du) > 1e-15) {   /* s(tau) = (tau du + k) / dd in [0, 1] */
        double s0 = -k / du, s1 = (dd - k) / du; if (s0 > s1) { double w = s0; s0 = s1; s1 = w; }
        if (s0 > t1) t1 = s0; if (s1 < t2) t2 = s1;
    } else { double s = k / dd; if (s < 0 || s > 1) return; }
    if (t1 > t2 || t1 < -1e299 || t2 > 1e299) return;
    unsigned wa = 0, wb = 0; if (c3_ct_on) { wa = c3_ct_seg(A[0] + t1, c, zc, A, B); wb = c3_ct_seg(A[0] + t2, c, zc, A, B); }
    iv_take(A[0] + t1, A[0] + t2, wa, wb, lo, hi, hit);
}
/* convex polygon P[0..np) (CCW about its normal nrm), the slab |n.(X - P0)| < R over it */
static void push_face(const double (*P)[3], int np, const double *nrm, double c, double zc, double R, double *lo, double *hi, int *hit) {
    double t1 = -1e300, t2 = 1e300;
    double h0 = nrm[1] * (c - P[0][1]) + nrm[2] * (zc - P[0][2]) - nrm[0] * P[0][0];   /* h(t) = nrm[0] t + h0 */
    if (fabs(nrm[0]) > 1e-15) { double a = (-R - h0) / nrm[0], b = (R - h0) / nrm[0]; if (a > b) { double w = a; a = b; b = w; } t1 = a; t2 = b; }
    else if (fabs(h0) >= R) return;
    for (int i = 0; i < np; i++) {   /* the prism over the polygon: (X(t) - Pi) . (n x ei) >= 0 */
        const double *A = P[i], *B = P[(i + 1) % np];
        double e[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
        double m[3] = { nrm[1] * e[2] - nrm[2] * e[1], nrm[2] * e[0] - nrm[0] * e[2], nrm[0] * e[1] - nrm[1] * e[0] };
        double k = (c - A[1]) * m[1] + (zc - A[2]) * m[2] - A[0] * m[0];   /* m0 t + k >= 0 */
        if (fabs(m[0]) < 1e-15) { if (k < 0) return; continue; }
        double t = -k / m[0];
        if (m[0] > 0) { if (t > t1) t1 = t; } else { if (t < t2) t2 = t; }
        if (t1 > t2) return;
    }
    if (t1 < -1e299 || t2 > 1e299) return;
    unsigned wa = 0, wb = 0;
    if (c3_ct_on) {   /* the facet's normal turned towards the centre */
        if (c3_ct_cyl) wa = wb = C3_CT_CYL;
        else { double nxy = hypot(nrm[0], nrm[1]), ha = nrm[0] * t1 + h0, hb = nrm[0] * t2 + h0;
            wa = c3_ct_ball(nxy, ha < 0 ? -nrm[2] : nrm[2]); wb = c3_ct_ball(nxy, hb < 0 ? -nrm[2] : nrm[2]); }
    }
    iv_take(t1, t2, wa, wb, lo, hi, hit);
}
/* clip the triangle at z = zc: below (keep z <= zc) or above (z >= zc); returns the vertex count (<= 4) */
static int clip_z(const double (*T)[3], int n, double zc, int above, double (*out)[3]) {
    int k = 0;
    for (int i = 0; i < n; i++) {
        const double *A = T[i], *B = T[(i + 1) % n];
        int ia = above ? A[2] >= zc : A[2] <= zc, ib = above ? B[2] >= zc : B[2] <= zc;
        if (ia) { memcpy(out[k], A, sizeof(double) * 3); k++; }
        if (ia != ib) { double s = (zc - A[2]) / (B[2] - A[2]); for (int j = 0; j < 3; j++) out[k][j] = A[j] + s * (B[j] - A[j]); out[k][2] = zc; k++; }
    }
    return k;
}
/* one triangle (fibre coordinates u, v, z) against the fibre v = c of the shape: a ball of radius R centred at zc with a
   cylinder of radius R above its centre (r(h) of FUN_18013fff0 with the flat part removed, plan 4.5). 1 = hit. */
static int push_tri(const double (*T)[3], double c, double zc, double R, double *lo, double *hi) {
    int hit = 0;
    double lowp[5][3], upp[5][3];
    int nl = clip_z(T, 3, zc, 0, lowp), nu = clip_z(T, 3, zc, 1, upp);
    if (nl >= 3) {                                    /* the ball against the part below its centre */
        double e1[3], e2[3], nr[3];
        for (int j = 0; j < 3; j++) { e1[j] = T[1][j] - T[0][j]; e2[j] = T[2][j] - T[0][j]; }
        nr[0] = e1[1] * e2[2] - e1[2] * e2[1]; nr[1] = e1[2] * e2[0] - e1[0] * e2[2]; nr[2] = e1[0] * e2[1] - e1[1] * e2[0];
        double L = sqrt(nr[0] * nr[0] + nr[1] * nr[1] + nr[2] * nr[2]);
        if (L > 1e-18) { for (int j = 0; j < 3; j++) nr[j] /= L; push_face((const double (*)[3])lowp, nl, nr, c, zc, R, lo, hi, &hit); }
        for (int i = 0; i < nl; i++) { push_vertex(lowp[i], c, zc, R, lo, hi, &hit); push_edge(lowp[i], lowp[(i + 1) % nl], c, zc, R, lo, hi, &hit); }
    }
    if (nu >= 3) {                                    /* the cylinder against the part above: horizontal distance */
        c3_ct_cyl = 1;
        double q[5][3];
        for (int i = 0; i < nu; i++) { q[i][0] = upp[i][0]; q[i][1] = upp[i][1]; q[i][2] = 0; }
        double A2 = 0; for (int i = 0; i < nu; i++) A2 += q[i][0] * q[(i + 1) % nu][1] - q[(i + 1) % nu][0] * q[i][1];
        if (fabs(A2) > 1e-18) { double nr[3] = { 0, 0, A2 > 0 ? 1 : -1 }; push_face((const double (*)[3])q, nu, nr, c, 0, R > 0 ? R : 1e-12, lo, hi, &hit); }
        for (int i = 0; i < nu; i++) { push_vertex(q[i], c, 0, R, lo, hi, &hit); push_edge(q[i], q[(i + 1) % nu], c, 0, R, lo, hi, &hit); }
        c3_ct_cyl = 0;
    }
    return hit;
}

static void quad_add(CPaths *q, int *cap, double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, int swap) {
    if (q->n == *cap) { *cap = *cap ? 2 * *cap : 256; q->p = realloc(q->p, sizeof(CPath) * *cap); }
    double *xy = malloc(sizeof(double) * 8), v[8] = { x0, y0, x1, y1, x2, y2, x3, y3 };
    for (int i = 0; i < 4; i++) { xy[2 * i] = swap ? v[2 * i + 1] : v[2 * i]; xy[2 * i + 1] = swap ? v[2 * i] : v[2 * i + 1]; }
    q->p[q->n].xy = xy; q->p[q->n].n = 4;
    if (cl_area(&q->p[q->n]) < 0) { for (int i = 0; i < 2; i++) { double a = xy[2 * i], b = xy[2 * i + 1]; xy[2 * i] = xy[2 * (3 - i)]; xy[2 * i + 1] = xy[2 * (3 - i) + 1]; xy[2 * (3 - i)] = a; xy[2 * (3 - i) + 1] = b; } }
    q->n++;
}
/* ours: the fibres' intervals -> polygons (quads between overlapping intervals of neighbouring fibres) */
static void fibres_quads(C3Fib *fb, int nf, const double *pos, int swap, CPaths *q, int *cap) {
    for (int j = 0; j + 1 < nf; j++) {
        C3Fib *a = &fb[j], *b = &fb[j + 1];
        for (int i = 0, k = 0; i < a->n && k < b->n; ) {
            double o0 = fmax(a->iv[i].lo, b->iv[k].lo), o1 = fmin(a->iv[i].hi, b->iv[k].hi);
            if (o0 <= o1) quad_add(q, cap, a->iv[i].lo, pos[j], a->iv[i].hi, pos[j], b->iv[k].hi, pos[j + 1], b->iv[k].lo, pos[j + 1], swap);
            if (a->iv[i].hi < b->iv[k].hi) i++; else k++;
        }
    }
}

/* --------------------------------------------------------------------------------------------- McHmm's weave loops (Waterline) */
/* The CWeave's fibres -> closed loops, McHmm's own traversal (mchmm.dll; "wd" = decomp/hst3d_weave_drop.c, formats/hst3d_port_plan.md 4.5
   "Weave -> loops (decoded 2026-11-13)"). Set A10 = the CWeave's fibre set +0x10 (fibres at x, intervals along y; node set byte 1), A58 = set +0x58
   (fibres at y, intervals along x; node set byte 0). Each interval keeps c0 / c1 (+0x30 / +0x34): the crossing fibres it covers are c0 .. c1 - 1.
   1. FUN_18012d020 (wd l.19677): the border closing (FUN_18012cde0 at the border crossings, FUN_18012cf20 joins touching intervals).
   2. FUN_180128f30 (wd l.16694): FUN_18012f730 (wd l.21919) on A10 then A58 alternately until two rounds change nothing (a change in round 11:
      McHmm's assert "Machining\Weave.cpp" - refused here): a crossing that the other set calls forbidden but this fibre's nearest interval does
      not cover extends that interval to the crossing when the interval ends in the next cell, else gets a point interval (" twist ",
      FUN_180130300 wd l.22500); an interval covering no crossing joins a neighbour ending in the same cell, and with the merge flag any two
      that end in one cell join.
   3. FUN_180129b00 (wd l.17307): a node per interval end inside the grid (FUN_1801312c0 wd l.23293), sorted by the cell before it (FUN_18012d7a0 /
      FUN_18012e9e0); each node becomes the 'next' of the first free node whose cell before it is the cell after the node; a cell entered twice
      goes to FUN_18012a140 (wd l.17669), which without WeaveResolveAmbiguous pairs the cell's nodes in their order round the cell (FUN_180129df0 /
      FUN_18012efa0), each with the one before it (its last argument 0, FUN_180271330 hmm-plans1 l.5266).
   4. The loop points are the interval ends (FUN_1804031a0 hz l.6340-6420). */
/* an interval: lo / hi, the crossing cells c0 / c1 (+0x30 / +0x34) and the clipped flags of its two ends (the contact words +0x18 / +0x28 with the top bit set,
   0x80000000: an end clipped to the fibre window by FUN_180125b70, the boundary fill FUN_180131810, a point interval of FUN_18012cde0 / FUN_180130300);
   FUN_18040b6f0 drops a loop vertex whose contact has it */
typedef struct { double lo, hi; int c0, c1; unsigned k0, k1; } WvIv;   /* k0 / k1: the contact words of the ends (c3_ct; | 0x80000000 = clipped) */
#define WV_CLIP 0x80000000u
typedef struct { WvIv *v; int n, cap; double pos; } WvFib;
typedef struct { WvFib *f; int n; double elo, ehi; } WvSet;   /* fibres in position order; elo / ehi = the extent along the fibres */
typedef struct { char c, e; int f, iv, k, next; } WvNode;
static void wv_ins(WvFib *f, int at, WvIv x) {
    if (f->n == f->cap) { f->cap = f->cap ? 2 * f->cap : 8; f->v = realloc(f->v, sizeof(WvIv) * f->cap); }
    memmove(f->v + at + 1, f->v + at, sizeof(WvIv) * (f->n - at)); f->v[at] = x; f->n++;
}
static void wv_del(WvFib *f, int at) { memmove(f->v + at, f->v + at + 1, sizeof(WvIv) * (f->n - at - 1)); f->n--; }
/* the fibre's lookup (CFibre vtable +0x10, as FUN_18012cde0 uses it): the first interval ending at or after v, covered when it starts at or before v */
static int wv_covers(const WvFib *f, double v) { for (int k = 0; k < f->n; k++) { if (f->v[k].hi >= v) return f->v[k].lo <= v; } return 0; }
/* the interval add of the fibre (CFibre vtable +0x18, FUN_180010ac0 -> FUN_18005fd30, mchmm.dll): the union. k = the first interval ending at or after lo
   (FUN_18005fcb0); when it starts at or before hi: a lower lo replaces its lo and lo contact, the following intervals starting at or before hi are
   joined (their hi and hi contact taken), then a higher hi replaces hi and hi contact; else the new interval is inserted at k (FUN_1800600d0) */
static void wv_add(WvFib *f, double lo, double hi, unsigned k0, unsigned k1) {
    int k = 0; while (k < f->n && f->v[k].hi < lo) k++;
    if (k < f->n && f->v[k].lo <= hi) {
        if (lo < f->v[k].lo) { f->v[k].lo = lo; f->v[k].k0 = k0; }
        while (k + 1 < f->n && f->v[k + 1].lo <= hi) { f->v[k].hi = f->v[k + 1].hi; f->v[k].k1 = f->v[k + 1].k1; wv_del(f, k + 1); }
        if (hi > f->v[k].hi) { f->v[k].hi = hi; f->v[k].k1 = k1; }
    } else { WvIv x = { lo, hi, 0, 0, k0, k1 }; wv_ins(f, k, x); }
}
/* FUN_18012d020 */
static void wv_border(WvSet *A10, WvSet *A58) {
    int i = A10->n - 1; for (; i >= 0; i--) if (!wv_covers(&A10->f[i], A10->ehi)) break;
    if (i == -1) wv_add(&A58->f[A58->n - 1], A58->elo, A58->ehi, WV_CLIP, WV_CLIP);   /* contacts 0x80000000 */
    int j = A58->n - 1; for (; j >= 0; j--) if (!wv_covers(&A58->f[j], A58->ehi)) break;
    if (j == -1) wv_add(&A10->f[A10->n - 1], A10->elo, A10->ehi, WV_CLIP, WV_CLIP);
    #define WV_CDE0(I, J) do { WvFib *a_ = &A10->f[I], *b_ = &A58->f[J]; double x_ = a_->pos, y_ = b_->pos; int b3_ = wv_covers(a_, y_), b4_ = wv_covers(b_, x_); \
        if (b3_ && !b4_) wv_add(b_, x_, x_, WV_CLIP, WV_CLIP); else if (!b3_ && b4_) wv_add(a_, y_, y_, WV_CLIP, WV_CLIP); } while (0)   /* FUN_18012cde0 */
    for (i = A10->n - 1; i >= 0; i--) { WV_CDE0(i, A58->n - 1); WV_CDE0(i, 0); }
    for (j = A58->n - 1; j >= 0; j--) { WV_CDE0(A10->n - 1, j); WV_CDE0(0, j); }
    #undef WV_CDE0
    for (int s = 0; s < 2; s++) { WvSet *S = s ? A58 : A10;   /* FUN_18012cf20: an interval ending where the next starts joins it */
        for (int q = 0; q < S->n; q++) { WvFib *f = &S->f[q]; for (int k = 0; k + 1 < f->n; ) { if (f->v[k].hi == f->v[k + 1].lo) { f->v[k].hi = f->v[k + 1].hi; f->v[k].k1 = f->v[k + 1].k1; wv_del(f, k + 1); } else k++; } } }
}
/* FUN_1801314e0: c0 = the first crossing fibre at or after lo, c1 = the last at or before hi (from the even spacing, one step corrected) */
static void wv_cells(const WvSet *B, WvIv *x) {
    int n = B->n; double P0 = B->f[0].pos, P1 = B->f[n - 1].pos;
    if (x->hi < P0 || P1 < x->lo) return;
    int c = (int)(((x->lo - P0) / (P1 - P0)) * (double)(n - 1)) + 1; if (c < 1) c = 0; if (c > n - 1) c = n - 1;
    if (c == 0 || B->f[c - 1].pos < x->lo) { if (c != n - 1 && B->f[c].pos < x->lo) c++; } else c--;
    x->c0 = c;
    c = (int)(((x->hi - P0) / (P1 - P0)) * (double)(n - 1)); if (c < 1) c = 0; if (c > n - 1) c = n - 1;
    if (c != n - 1 && B->f[c + 1].pos <= x->hi) c++; else if (c != 0 && x->hi < B->f[c].pos) c--;
    x->c1 = c;
}
typedef struct { double v; char t; int j; } WvEv;
static int wv_evcmp(const void *a, const void *b) {   /* FUN_180133a30: the value, then lo before hi, then the fibre */
    const WvEv *p = a, *q = b;
    if (p->v != q->v) return p->v < q->v ? -1 : 1;
    if (p->t != q->t) return p->t < q->t ? -1 : 1;
    return (p->j > q->j) - (p->j < q->j);
}
/* FUN_18012f730 + FUN_180130300 on A against B: 1 = nothing changed */
static int wv_consistent(WvSet *A, const WvSet *B, int mflag) {
    int nev = 0; for (int j = 0; j < B->n; j++) nev += 2 * B->f[j].n;
    WvEv *ev = malloc(sizeof(WvEv) * (nev + 1)); int m = 0;
    for (int j = 0; j < B->n; j++) for (int k = 0; k < B->f[j].n; k++) { ev[m++] = (WvEv){ B->f[j].v[k].lo, 0, j }; ev[m++] = (WvEv){ B->f[j].v[k].hi, 1, j }; }
    qsort(ev, m, sizeof(WvEv), wv_evcmp);
    char *in = calloc(B->n, 1); int e = 0, changes = 0;
    for (int i = 0; i < A->n; i++) {
        WvFib *f = &A->f[i]; double p = f->pos;
        while (e < m && (ev[e].v < p || (ev[e].t == 0 && ev[e].v == p))) { in[ev[e].j] = ev[e].t == 0; e++; }   /* the break test of FUN_18012f730 */
        for (int k = 0; k < f->n; k++) { wv_cells(B, &f->v[k]); f->v[k].c1++; }
        int k = 0;
        for (int j = 0; j < B->n; j++) {
            double bp = B->f[j].pos;
            if (k < f->n - 1) do { if (bp - f->v[k].hi <= f->v[k + 1].lo - bp) break; k++; } while (k < f->n - 1);
            if (!in[j]) continue;
            int twist = 0;
            if (!f->n) twist = 1;
            else { WvIv *x = &f->v[k];
                if (j < x->c0) { if (x->c0 - 1 != j) twist = 1; else { x->lo = bp; x->c0 = j; changes++; } }
                else if (x->c1 <= j) { if (x->c1 != j) twist = 1; else { x->hi = bp; x->c1 = j + 1; changes++; } } }
            if (twist) { WvIv x = { bp, bp, j, j + 1, WV_CLIP, WV_CLIP };   /* contacts 0x80000000 (FUN_180130300) */
                if (!f->n) { wv_ins(f, 0, x); k = 0; }
                else if (bp <= f->v[k].hi) wv_ins(f, k, x);
                else { wv_ins(f, k + 1, x); k++; }
                changes++; }
        }
        for (int q = 0; q < f->n; q++) {
            if (f->v[q].c0 == f->v[q].c1) {
                if (q > 0 && f->v[q - 1].c1 == f->v[q].c0) { f->v[q - 1].hi = f->v[q].hi; f->v[q - 1].k1 = f->v[q].k1; f->v[q - 1].c1 = f->v[q].c1; wv_del(f, q); q--; changes++; }
                if (q != f->n - 1 && f->v[q + 1].c0 == f->v[q].c1) { f->v[q + 1].lo = f->v[q].lo; f->v[q + 1].k0 = f->v[q].k0; f->v[q + 1].c0 = f->v[q].c0; wv_del(f, q); q--; changes++; }
            }
            if (mflag && q > 0 && f->v[q - 1].c1 == f->v[q].c0) { f->v[q - 1].hi = f->v[q].hi; f->v[q - 1].k1 = f->v[q].k1; f->v[q - 1].c1 = f->v[q].c1; wv_del(f, q); q--; changes++; }
        }
    }
    free(ev); free(in);
    return changes == 0;
}
/* the cell keys (FUN_180129b00): 'after' with t = f + (e == c), 'before' (the sort key) with t = f + (e != c); set byte 0: (k, t), 1: (t, k) */
static void wv_key(const WvNode *n, int before, int *a, int *b) {
    int t = n->f + (before ? n->e != n->c : n->e == n->c);
    if (n->c == 0) { *a = n->k; *b = t; } else { *a = t; *b = n->k; }
}
static int wv_kcmp(const WvNode *p, const WvNode *q) { int a, b, c, d; wv_key(p, 1, &a, &b); wv_key(q, 1, &c, &d); return a != c ? (a < c ? -1 : 1) : (b != d ? (b < d ? -1 : 1) : 0); }
static int wv_nodecmp(const void *a, const void *b) { return wv_kcmp(a, b); }
typedef struct { int side, node, in, iv; } WvEnt;
static int wv_entless(const WvEnt *a, const WvEnt *b) {   /* FUN_18012efa0: the side (0 top, 1 right, 2 bottom, 3 left), along it by the interval index */
    if (a->side == b->side) { if (a->iv != b->iv) return (a->iv < b->iv) == (a->side == 3 || a->side == 0); return a->in != 0; }
    return a->side < b->side;
}
/* FUN_18012a140's probe with WeaveResolveAmbiguous: FUN_1801127c0 (wd l.1461) builds a fibre on the line through two points (d = the unit direction,
   n = (-dy, dx), offset c = n . p) clipped to the cell (FUN_1801121d0: the line's parameter range inside [x(col - 1), x(col)] x [y(row - 1), y(row)]),
   FUN_180225ba0 drops the model on it with the weave's tool at the weave's Z; the cell's ends are joined when the result is one interval spanning
   the whole clipped range. Here the drop is push_tri in the line's frame (u = d . p along, v = n . p across). */
typedef struct { const C3Mesh *m; double ztip, Rb, zc; } WvProbe;
static int wv_probe_full(const WvProbe *pr, const double *pa, const double *pb, double x0, double x1, double y0, double y1) {
    double dx = pb[0] - pa[0], dy = pb[1] - pa[1], L = sqrt(dx * dx + dy * dy); if (L == 0.0) L = 1.0;
    double d[2] = { dx / L, dy / L }, n[2] = { -d[1], d[0] }, c = n[0] * pa[0] + n[1] * pa[1];
    double s0 = -1e300, s1 = 1e300;                       /* p(s) = s d + c n */
    if (d[0] != 0.0) { double a = (x0 - n[0] * c) / d[0], b = (x1 - n[0] * c) / d[0]; s0 = fmax(s0, fmin(a, b)); s1 = fmin(s1, fmax(a, b)); }
    if (d[1] != 0.0) { double a = (y0 - n[1] * c) / d[1], b = (y1 - n[1] * c) / d[1]; s0 = fmax(s0, fmin(a, b)); s1 = fmin(s1, fmax(a, b)); }
    if (!(s0 <= s1)) return 0;
    C3Fib f = { 0 }; double reach = pr->Rb + 0.003;
    for (int i = 0; i < pr->m->n; i++) {
        const double *t = pr->m->t + 9 * i;
        if (fmax(t[2], fmax(t[5], t[8])) - pr->ztip <= 0) continue;
        double T[3][3], vlo = 1e300, vhi = -1e300, ulo = 1e300, uhi = -1e300;
        for (int k = 0; k < 3; k++) { T[k][0] = d[0] * t[3 * k] + d[1] * t[3 * k + 1]; T[k][1] = n[0] * t[3 * k] + n[1] * t[3 * k + 1]; T[k][2] = t[3 * k + 2];
            vlo = fmin(vlo, T[k][1]); vhi = fmax(vhi, T[k][1]); ulo = fmin(ulo, T[k][0]); uhi = fmax(uhi, T[k][0]); }
        if (vlo - reach > c || vhi + reach < c || uhi + reach < s0 || ulo - reach > s1) continue;
        double lo, hi; if (push_tri((const double (*)[3])T, c, pr->zc, pr->Rb, &lo, &hi)) fib_add(&f, lo, hi);
    }
    fib_merge(&f);
    int full = 0; for (int k = 0; k < f.n; k++) if (f.iv[k].lo <= s0 && f.iv[k].hi >= s1) full = 1;
    free(f.iv); return full;
}
/* The boundary fill of the slice weave (Waterline; formats/hst3d_port_plan.md 7 "The weave box and the boundary fill"). FUN_1803fb4f0 (hz l.1209-1215) calls
   FUN_18012bf60(weave, the boundary collection Eva +0x118, r = BoundaryOutwardsOffsetRad, r != 0, 1): FUN_180131810 (wd l.23535) on both fibre sets. Per
   fibre, from P0 = (position, extent lo) to P1 = (position, extent hi) (set +0x10; (lo, position) .. for set +0x58), FUN_180289d90 (mchmm.dll) lists the
   parameter ranges of the fibre inside the boundaries grown by r:
   - FUN_180289390: the fibre is extended past the collection's box (+0x18..+0x30): s = max(t, 1) + 1.01, t = the largest projection of the box corners
     ((corner - P0) . d / |d|^2, d = P1 - P0); P1' = P0 + s d;
   - MeasureRadSegs2D (FUN_18023ada0) over the boundary elements whose box meets the box of P0 P1' grown by 1.1 r (FUN_18028a970 lines, FUN_18028ab70
     arcs): FUN_180057040 the crossings with the fibre's line (a crossing when the ends' sides differ, side = cross > 0; its parameter = the projection of the
     intersection point), FUN_180055b50 the range where the fibre is within r of the element (clamped to [0, 1], not kept when it is [1, 1]);
   - FUN_180289520: the crossings at t >= 0 sorted; P0 is inside when their count is odd; the inside ranges between them and the within-r ranges joined
     (with r = 0 only the inside ranges), each scaled by s, its hi capped at 1, those starting at 1 or after left out.
   FUN_180131810 with fill = 1 then puts on the fibre the ranges outside those (from the extent's start to the first lo unless that is 0.0, between each
   hi and the next lo, from the last hi to the extent's end unless that is 1.0; all of the fibre when there are none), t -> (1 - t) lo + t hi, each with
   both contact words 0x80000000. Ours: the boundaries are the containment polygon (its arcs as chords, c3_containment); the within-r range of a line is
   computed with round ends (BuildToolBoxSetRoundEverything; the boundary collection's +0x128 switch of FUN_180055b50 is not traced). */
typedef struct { const CPaths *B; double r, box[4]; } WvFill;
static int wv_dcmp(const void *a, const void *b) { double d = *(const double *)a - *(const double *)b; return d < 0 ? -1 : d > 0; }
/* the range of t in [0, 1] where P0 + t D lies within r of segment A B (FUN_180055b50: the line's distance to a segment is convex along it) */
static int wv_radseg(const double *P0, const double *D, const double *A, const double *B, double r, double *lo, double *hi) {
    double a = 1e300, b = -1e300, dd = D[0] * D[0] + D[1] * D[1];
    for (int e = 0; e < 2; e++) {                           /* the end discs */
        const double *Q = e ? B : A; double w[2] = { P0[0] - Q[0], P0[1] - Q[1] };
        double qb = w[0] * D[0] + w[1] * D[1], qc = w[0] * w[0] + w[1] * w[1] - r * r, disc = qb * qb - dd * qc;
        if (disc >= 0 && dd > 0) { double sq = sqrt(disc); a = fmin(a, (-qb - sq) / dd); b = fmax(b, (-qb + sq) / dd); }
    }
    double E[2] = { B[0] - A[0], B[1] - A[1] }, ee = E[0] * E[0] + E[1] * E[1];
    if (ee > 0) {                                           /* the band: 0 <= (P - A) . E <= |E|^2 and |(P - A) x E| <= r |E| */
        double t0 = -1e300, t1 = 1e300, w[2] = { P0[0] - A[0], P0[1] - A[1] }, L = sqrt(ee);
        double p0 = w[0] * E[0] + w[1] * E[1], p1 = D[0] * E[0] + D[1] * E[1];
        double c0 = w[0] * E[1] - w[1] * E[0], c1 = D[0] * E[1] - D[1] * E[0];
        #define WV_SLAB(v0, v1, lo_, hi_) do { if ((v1) == 0) { if ((v0) < (lo_) || (v0) > (hi_)) t1 = -1e300; } \
            else { double u_ = ((lo_) - (v0)) / (v1), v_ = ((hi_) - (v0)) / (v1); t0 = fmax(t0, fmin(u_, v_)); t1 = fmin(t1, fmax(u_, v_)); } } while (0)
        WV_SLAB(p0, p1, 0.0, ee); WV_SLAB(c0, c1, -r * L, r * L);
        #undef WV_SLAB
        if (t0 <= t1) { a = fmin(a, t0); b = fmax(b, t1); }
    }
    if (a > b || b < 0 || a > 1) return 0;
    *lo = fmax(a, 0.0); *hi = fmin(b, 1.0);
    return !(*lo == 1.0 && *hi == 1.0);
}
/* the fill intervals of one fibre (wv_add with both ends clipped); along_x: the fibre runs along x (set +0x58) */
static void wv_fill(const WvFill *F, WvFib *f, int along_x, double elo, double ehi) {
    double P0[2], P1[2];
    if (along_x) { P0[0] = elo; P0[1] = f->pos; P1[0] = ehi; P1[1] = f->pos; } else { P0[0] = f->pos; P0[1] = elo; P1[0] = f->pos; P1[1] = ehi; }
    double d[2] = { P1[0] - P0[0], P1[1] - P0[1] }, dd = d[0] * d[0] + d[1] * d[1];
    if (!(dd > 0)) return;
    double pm = -1e300;                                     /* FUN_180289390 */
    for (int c = 0; c < 4; c++) { double cx = F->box[c & 1 ? 2 : 0], cy = F->box[c & 2 ? 3 : 1]; pm = fmax(pm, d[0] * cx + d[1] * cy); }
    double s = fmax((pm - (d[0] * P0[0] + d[1] * P0[1])) / dd, 1.0) + 1.01, D[2] = { s * d[0], s * d[1] };
    double qb[4] = { fmin(P0[0], P0[0] + D[0]) - 1.1 * F->r, fmin(P0[1], P0[1] + D[1]) - 1.1 * F->r, fmax(P0[0], P0[0] + D[0]) + 1.1 * F->r, fmax(P0[1], P0[1] + D[1]) + 1.1 * F->r };
    double DD = D[0] * D[0] + D[1] * D[1];
    int nc = 0, cc = 64, ni = 0, ci = 64; double *cr = malloc(sizeof(double) * cc), *iv = malloc(sizeof(double) * 2 * ci);
    for (int l = 0; l < F->B->n; l++) { const CPath *L = &F->B->p[l];
        for (int a = 0; a < L->n; a++) { const double *A = L->xy + 2 * a, *B = L->xy + 2 * ((a + 1) % L->n);
            if (fmax(A[0], B[0]) < qb[0] || fmin(A[0], B[0]) > qb[2] || fmax(A[1], B[1]) < qb[1] || fmin(A[1], B[1]) > qb[3]) continue;   /* FUN_18028a970 */
            double lo, hi;
            if (F->r != 0.0 && wv_radseg(P0, D, A, B, F->r, &lo, &hi)) {   /* FUN_180055b50 */
                if (ni == ci) { ci *= 2; iv = realloc(iv, sizeof(double) * 2 * ci); }
                iv[2 * ni] = lo; iv[2 * ni + 1] = hi; ni++; }
            double sa = -D[1] * (A[0] - P0[0]) + D[0] * (A[1] - P0[1]), sb = -D[1] * (B[0] - P0[0]) + D[0] * (B[1] - P0[1]);   /* FUN_180057040 */
            if ((0.0 < sa) == (0.0 < sb)) continue;
            double u = sa / (sa - sb), X[2] = { A[0] + u * (B[0] - A[0]), A[1] + u * (B[1] - A[1]) };
            if (nc == cc) { cc *= 2; cr = realloc(cr, sizeof(double) * cc); }
            cr[nc++] = DD == 0.0 ? 0.5 : (D[0] * (X[0] - P0[0]) + D[1] * (X[1] - P0[1])) / DD; } }
    qsort(cr, nc, sizeof(double), wv_dcmp);
    int c0 = 0; while (c0 < nc && cr[c0] < 0.0) c0++;
    int in = 0, mi = (nc - c0) / 2 + 1; double *ins = malloc(sizeof(double) * 2 * (mi + ni + 1));
    for (int q = ((nc - c0) & 1) ? c0 - 1 : c0; q < nc; q += 2) {   /* FUN_180289520: the inside ranges (P0 inside when the count is odd) */
        double lo = q == c0 - 1 ? 0.0 : s * cr[q]; if (lo >= 1.0) continue;
        double hi = q + 1 < nc ? fmin(s * cr[q + 1], 1.0) : 1.0; ins[2 * in] = lo; ins[2 * in + 1] = hi; in++; }
    for (int q = 0; q < ni; q++) { double lo = s * iv[2 * q]; if (lo >= 1.0) continue; ins[2 * in] = lo; ins[2 * in + 1] = fmin(s * iv[2 * q + 1], 1.0); in++; }
    C3Fib u = { 0 }; for (int q = 0; q < in; q++) fib_add(&u, ins[2 * q], ins[2 * q + 1]);
    fib_merge(&u);                                          /* the inside ranges joined with the within-r ranges */
    #define WV_T(t) ((1.0 - (t)) * elo + (t) * ehi)
    if (!u.n) wv_add(f, elo, ehi, WV_CLIP, WV_CLIP);                    /* FUN_180131810 fill = 1 */
    else for (int q = 0; q <= u.n; q++) {
        if (q == 0 && u.iv[0].lo == 0.0) continue;
        if (q == u.n && u.iv[u.n - 1].hi == 1.0) continue;
        double lo = q == 0 ? elo : WV_T(u.iv[q - 1].hi), hi = q == u.n ? ehi : WV_T(u.iv[q].lo);
        wv_add(f, lo, hi, WV_CLIP, WV_CLIP);
    }
    #undef WV_T
    free(u.iv); free(cr); free(iv); free(ins);
}
/* McHmm's pockets of a weave level (FUN_18012b1e0 wd l.18359-18719, the list Eva +0x1430 +0x240 that FUN_1803faee0 walks with Eva +0x1438 and
   FUN_18040b6f0 writes): every loop of the node ring (all of them, also those of one or two nodes, which the union skips) in McHmm's loop order with
   its start node first, the vertices' contact words, and the pockets as loop indices (each pocket's loops in order, -1 after each pocket) */
typedef struct { CPaths L; unsigned **fl; int *pk; int npk; } WvPockets;
static _Thread_local WvPockets *c3_weave_pk;
static void wv_pockets_free(WvPockets *P) { for (int i = 0; i < P->L.n; i++) { free(P->L.p[i].xy); free(P->fl[i]); } free(P->L.p); free(P->fl); free(P->pk); memset(P, 0, sizeof *P); }
/* the loops of the forbidden region (above); 0 = refused (*err). fill (Waterline): the boundary fill first (FUN_18012bf60 on the template weave before
   the model is pushed); oflags: per loop, the clipped flags of its vertices */
static int wv_loops(C3Fib *fx, int nx, const double *py, C3Fib *fy, int ny, const double *px, const WvProbe *pr, int resolve, const WvFill *fill, CPaths *out, unsigned ***oflags, const char **err) {
    /* fx[j]: our fibre at y = py[j], intervals in x = McHmm's set +0x58; fy[i]: at x = px[i], intervals in y = set +0x10 */
    WvSet A10 = { calloc(ny, sizeof(WvFib)), ny, py[0], py[nx - 1] }, A58 = { calloc(nx, sizeof(WvFib)), nx, px[0], px[ny - 1] };
    for (int s = 0; s < 2; s++) { WvSet *S = s ? &A58 : &A10; C3Fib *src = s ? fx : fy; const double *pp = s ? py : px;
        for (int i = 0; i < S->n; i++) { S->f[i].pos = pp[i];
            if (fill) wv_fill(fill, &S->f[i], s, S->elo, S->ehi);
            for (int k = 0; k < src[i].n; k++) { double lo = fmax(src[i].iv[k].lo, S->elo), hi = fmin(src[i].iv[k].hi, S->ehi);   /* clipped to the fibre window (FUN_180125b70: the end's contact | 0x80000000) */
                if (lo <= hi) wv_add(&S->f[i], lo, hi, src[i].iv[k].lo < S->elo ? WV_CLIP : src[i].iv[k].w0, src[i].iv[k].hi > S->ehi ? WV_CLIP : src[i].iv[k].w1); } } }
    int ok = 1; *err = NULL;
    wv_border(&A10, &A58);
    int mflag = !resolve;                                  /* FUN_180271330: FUN_180128f30's flag = (no probe model && argument 4 == 0) */
    { int r = 0, last = 0;
      for (;;) { int same = r & 1 ? wv_consistent(&A58, &A10, mflag) : wv_consistent(&A10, &A58, mflag);
          if (!same) last = r + 1;
          if (r == 10) { ok = 0; *err = "the weave did not settle in 11 rounds (McHmm asserts there, Machining\\Weave.cpp)"; break; }
          r++; if (last + 2 <= r) break; } }
    {   /* MCAM_WL_FIBDUMP="S,A,B" (testing): fibres A..B after the consistency rounds, S = McHmm's node set byte (1: A10, 0: A58), as peloader's HMM_WFIB */
      const char *fs = getenv("MCAM_WL_FIBDUMP"); int s0, a0, b0;
      if (fs && sscanf(fs, "%d,%d,%d", &s0, &a0, &b0) == 3) { const WvSet *S = s0 ? &A10 : &A58;
          for (int f = a0; f <= b0 && f < S->n; f++) { fprintf(stderr, "wlfib %d %d %.17g %d\n", s0, f, S->f[f].pos, S->f[f].n);
              for (int k = 0; k < S->f[f].n; k++) fprintf(stderr, "wliv %.17g %.17g %d %08x %d %08x\n", S->f[f].v[k].lo, S->f[f].v[k].hi, S->f[f].v[k].c0, S->f[f].v[k].k0, S->f[f].v[k].c1, S->f[f].v[k].k1); } } }
    WvNode *nd = NULL; int nn = 0, cap = 0;
    for (int s = 0; ok && s < 2; s++) { WvSet *S = s ? &A58 : &A10; int nB = s ? A10.n : A58.n;   /* FUN_1801312c0: set +0x10 first */
        for (int i = 0; i < S->n; i++) for (int k = 0; k < S->f[i].n; k++) {
            if (nn + 2 > cap) { cap = cap ? 2 * cap : 256; nd = realloc(nd, sizeof(WvNode) * cap); }
            if (S->f[i].v[k].c0 != 0) nd[nn++] = (WvNode){ (char)(s ? 0 : 1), 0, i, k, S->f[i].v[k].c0, -1 };
            if (S->f[i].v[k].c1 != nB) nd[nn++] = (WvNode){ (char)(s ? 0 : 1), 1, i, k, S->f[i].v[k].c1, -1 }; } }
    int *amb = NULL, na = 0;
    if (ok && nn) {
        qsort(nd, nn, sizeof(WvNode), wv_nodecmp);
        amb = malloc(sizeof(int) * nn);
        for (int i = 0; i < nn; i++) {                    /* FUN_180129b00's linking */
            int a, b; wv_key(&nd[i], 0, &a, &b);
            int lo = -1, hi = nn - 1;
            while (hi - lo > 1) { int mid = (hi + lo) / 2, c, d; wv_key(&nd[mid], 1, &c, &d); if (c < a || (c <= a && d < b)) lo = mid; else hi = mid; }
            int m = hi, t = m;
            if (nd[m].next != -1) {
                t = m + 1;
                if (t < nn && nd[t].next == -1) amb[na++] = m;
                else { t = m + 2; while (t < nn && nd[t].next != -1) t++; }
            }
            if (t >= nn) { ok = 0; *err = "a weave node without a free node in its cell (the fibres are inconsistent)"; break; }
            nd[t].next = i;
        }
    }
    for (int q = 0; ok && q < na; q++) {                    /* FUN_18012a140 (wd l.17669) */
        int m = amb[q], cnt = 0; WvEnt *en = malloc(sizeof(WvEnt) * 2 * nn);
        for (int p = m; p < nn && wv_kcmp(&nd[p], &nd[m]) == 0; p++) {   /* FUN_180129df0 */
            const WvNode *P = &nd[p], *Q = &nd[P->next];
            en[cnt++] = (WvEnt){ P->c == 0 ? (P->e == 1 ? 2 : 0) : (P->e == 1 ? 1 : 3), p, 1, P->iv };
            en[cnt++] = (WvEnt){ Q->c == 0 ? (Q->e == 0 ? 2 : 0) : (Q->e == 0 ? 1 : 3), P->next, 0, Q->iv };
        }
        for (int x = 1; x < cnt; x++) { WvEnt t = en[x]; int y = x; while (y > 0 && wv_entless(&t, &en[y - 1])) { en[y] = en[y - 1]; y--; } en[y] = t; }   /* FUN_18012d960 (an insertion sort for these few) */
        #define WV_LINK(A, B) (nd[en[B].node].next = en[A].node)   /* entry B's node is followed by entry A's */
        if (!resolve) {
            for (int u = en[0].in == 0 ? 1 : 0; u < cnt; u += 2) WV_LINK(u == 0 ? cnt - 1 : u - 1, u);   /* argument 7 = 0: the one before */
        } else {
            if (en[0].in == 0) { WvEnt t = en[0]; memmove(en, en + 1, sizeof(WvEnt) * (cnt - 1)); en[cnt - 1] = t; }   /* start at an entering node */
            double pt[6][2];
            for (int u = 0; u < cnt && u < 6; u++) { const WvNode *N = &nd[en[u].node]; const WvSet *S = N->c ? &A10 : &A58; const WvIv *x = &S->f[N->f].v[N->iv]; double v = N->e ? x->hi : x->lo;
                if (N->c) { pt[u][0] = S->f[N->f].pos; pt[u][1] = v; } else { pt[u][0] = v; pt[u][1] = S->f[N->f].pos; } }
            int a, b; wv_key(&nd[en[0].node], 1, &a, &b);   /* the cell (column a, row b): between fibres a - 1, a of set +0x10 and b - 1, b of set +0x58 */
            double x0 = A10.f[a - 1].pos, x1 = A10.f[a].pos, y0 = A58.f[b - 1].pos, y1 = A58.f[b].pos;
            #define WV_MID(o, i, j) do { o[0] = (pt[i][0] + pt[j][0]) * 0.5; o[1] = (pt[i][1] + pt[j][1]) * 0.5; } while (0)
            if (cnt == 4) {
                double m0[2], m1[2];
                if (pt[0][0] != pt[1][0] || pt[0][1] != pt[1][1]) WV_MID(m0, 0, 1); else { m0[0] = pt[0][0]; m0[1] = pt[0][1]; }
                if (pt[2][0] != pt[3][0] || pt[2][1] != pt[3][1]) WV_MID(m1, 2, 3); else { m1[0] = pt[2][0]; m1[1] = pt[2][1]; }
                if (wv_probe_full(pr, m0, m1, x0, x1, y0, y1)) { WV_LINK(3, 0); WV_LINK(1, 2); } else { WV_LINK(1, 0); WV_LINK(3, 2); }
            } else if (cnt == 6) {
                double m0[2], m1[2], m2[2]; WV_MID(m0, 0, 1); WV_MID(m1, 2, 3); WV_MID(m2, 4, 5);   /* FUN_180054c20 */
                int f01 = wv_probe_full(pr, m0, m1, x0, x1, y0, y1), f02 = wv_probe_full(pr, m0, m2, x0, x1, y0, y1), f12 = wv_probe_full(pr, m1, m2, x0, x1, y0, y1);
                if (f01 && f02 && f12) { WV_LINK(5, 0); WV_LINK(1, 2); WV_LINK(3, 4); }
                else if (f01 && !f02 && !f12) { WV_LINK(3, 0); WV_LINK(1, 2); WV_LINK(5, 4); }
                else if (!f01 && !f02 && f12) { WV_LINK(1, 0); WV_LINK(5, 2); WV_LINK(3, 4); }
                else if (!f01 && f02 && !f12) { WV_LINK(5, 0); WV_LINK(1, 4); WV_LINK(3, 2); }
                else { WV_LINK(1, 0); WV_LINK(3, 2); WV_LINK(5, 4); }
            } else for (int u = 0; u + 1 < cnt; u += 2) WV_LINK(u + 1, u);
            #undef WV_MID
        }
        #undef WV_LINK
        free(en);
    }
    if (ok) {                                               /* the loops (FUN_18012b1e0 walks them; the points as FUN_1804031a0) */
        char *seen = calloc(nn ? nn : 1, 1);
        for (int s0 = 0; s0 < nn && ok; s0++) {
            if (seen[s0]) continue;
            int cnt = 0, p = s0; while (p >= 0 && !seen[p]) { seen[p] = 1; cnt++; p = nd[p].next; }
            if (p != s0) { ok = 0; *err = "a weave loop does not close"; break; }
            if (cnt < 3) continue;
            double *xy = malloc(sizeof(double) * 2 * cnt); unsigned *fl = malloc(sizeof(unsigned) * cnt); int w = 0; p = s0;
            do { const WvNode *N = &nd[p]; const WvSet *S = N->c ? &A10 : &A58; const WvIv *x = &S->f[N->f].v[N->iv]; double v = N->e ? x->hi : x->lo;
                 if (N->c) { xy[2 * w] = S->f[N->f].pos; xy[2 * w + 1] = v; } else { xy[2 * w] = v; xy[2 * w + 1] = S->f[N->f].pos; }
                 fl[w] = N->e ? x->k1 : x->k0;               /* the node's contact word (FUN_18040b6f0: +0x18 for a lo end, +0x28 for a hi end) */
                 w++; p = N->next; } while (p != s0);
            if (out->n % 64 == 0) { out->p = realloc(out->p, sizeof(CPath) * (out->n + 64)); if (oflags) *oflags = realloc(*oflags, sizeof(unsigned *) * (out->n + 64)); }
            if (oflags) (*oflags)[out->n] = fl; else free(fl);
            out->p[out->n].xy = xy; out->p[out->n].n = w; out->n++;
        }
        free(seen);
    }
    if (ok && c3_weave_pk) {
        /* FUN_18012b1e0 (wd l.18359-18719). Phase 1: in node order, a node whose interval end has no loop yet (end slot +0x38 lo / +0x3c hi = -1)
           starts loop L (entry {L's start node, pocket -1}) and its ring gets L in every end slot - the ring walked from the lowest node, as above.
           Phase 2: per loop L in order whose pocket is still -1, the fibre of L's start node: per gap between its intervals j - 1 and j, A = the
           loop of interval j - 1's hi end, B = the loop of interval j's lo end; A without a pocket -> a new pocket (FUN_18012d2c0) holding A and,
           when B != A, B (pockets of A and B = A); else B without one -> B appended to the pocket whose first loop is A's pocket (searched in all
           pockets but the last; none -> the last), B's pocket = A's. An entry is (loop, start node); FUN_18040b6f0 writes the start node's ring. */
        WvPockets *P = c3_weave_pk; memset(P, 0, sizeof *P);
        int *lid = malloc(sizeof(int) * (nn ? nn : 1)), *st = NULL, nl = 0;
        int *slot[2]; slot[0] = slot[1] = NULL;
        /* the end slots, per set (node c: 1 = A10, 0 = A58), fibre and interval: index of the first interval of each fibre */
        int *fo[2]; for (int c = 0; c < 2; c++) { const WvSet *S = c ? &A10 : &A58; fo[c] = malloc(sizeof(int) * (S->n + 1)); int t = 0;
            for (int i = 0; i < S->n; i++) { fo[c][i] = t; t += S->f[i].n; } fo[c][S->n] = t; slot[c] = malloc(sizeof(int) * 2 * (t ? t : 1)); for (int q = 0; q < 2 * t; q++) slot[c][q] = -1; }
        #define WV_SLOT(N) slot[(int)(N).c][2 * (fo[(int)(N).c][(N).f] + (N).iv) + (N).e]
        for (int i = 0; i < nn; i++) lid[i] = -1;
        for (int i = 0; i < nn; i++) {
            if (WV_SLOT(nd[i]) != -1) continue;
            st = realloc(st, sizeof(int) * (nl + 1)); st[nl] = i;
            int p = i, c = 0; do { WV_SLOT(nd[p]) = nl; lid[p] = nl; p = nd[p].next; } while (p != i && p >= 0 && ++c <= nn);
            nl++;
        }
        int *par = malloc(sizeof(int) * (nl ? nl : 1)); for (int L = 0; L < nl; L++) par[L] = -1;
        int **pkl = NULL, *pkn = NULL, npk = 0;   /* the pockets' loop lists */
        for (int L = 0; L < nl && ok; L++) {
            if (par[L] != -1) continue;
            const WvNode *N = &nd[st[L]]; const WvSet *S = N->c ? &A10 : &A58; const WvFib *F = &S->f[N->f]; int c = N->c, b0 = fo[c][N->f];
            for (int j = 1; j < F->n; j++) {
                int A = slot[c][2 * (b0 + j - 1) + 1], B = slot[c][2 * (b0 + j)];
                if (A < 0 || B < 0) { ok = 0; *err = "a weave gap without a loop at its ends (FUN_18012b1e0 would read before its table)"; break; }
                if (par[A] == -1) {
                    pkl = realloc(pkl, sizeof(int *) * (npk + 1)); pkn = realloc(pkn, sizeof(int) * (npk + 1)); pkl[npk] = malloc(sizeof(int) * 2); pkn[npk] = 0;
                    pkl[npk][pkn[npk]++] = A; par[A] = A;
                    if (B != A) { pkl[npk][pkn[npk]++] = B; par[B] = A; }
                    npk++;
                } else if (par[B] == -1) {
                    int q = 0; while (q < npk - 1 && pkl[q][0] != par[A]) q++;
                    pkl[q] = realloc(pkl[q], sizeof(int) * (pkn[q] + 1)); pkl[q][pkn[q]++] = B; par[B] = par[A];
                }
            }
        }
        #undef WV_SLOT
        if (ok) {   /* every loop's ring from its start node, and the pockets */
            P->L.p = calloc(nl ? nl : 1, sizeof(CPath)); P->fl = calloc(nl ? nl : 1, sizeof(unsigned *)); P->L.n = nl;
            for (int L = 0; L < nl; L++) {
                int cnt = 0, p = st[L]; do { cnt++; p = nd[p].next; } while (p != st[L] && cnt <= nn);
                double *xy = malloc(sizeof(double) * 2 * cnt); unsigned *fl = malloc(sizeof(unsigned) * cnt); int w = 0; p = st[L];
                do { const WvNode *N = &nd[p]; const WvSet *S = N->c ? &A10 : &A58; const WvIv *x = &S->f[N->f].v[N->iv]; double v = N->e ? x->hi : x->lo;
                     if (N->c) { xy[2 * w] = S->f[N->f].pos; xy[2 * w + 1] = v; } else { xy[2 * w] = v; xy[2 * w + 1] = S->f[N->f].pos; }
                     fl[w] = N->e ? x->k1 : x->k0; w++; p = N->next; } while (p != st[L] && w < cnt);
                P->L.p[L].xy = xy; P->L.p[L].n = w; P->fl[L] = fl;
            }
            for (int q = 0; q < npk; q++) for (int k = 0; k <= pkn[q]; k++) { P->pk = realloc(P->pk, sizeof(int) * (P->npk + 1)); P->pk[P->npk++] = k < pkn[q] ? pkl[q][k] : -1; }
        }
        for (int q = 0; q < npk; q++) free(pkl[q]);
        free(pkl); free(pkn); free(par); free(st); free(lid); free(fo[0]); free(fo[1]); free(slot[0]); free(slot[1]);
    }
    free(nd); free(amb);
    for (int s = 0; s < 2; s++) { WvSet *S = s ? &A58 : &A10; for (int i = 0; i < S->n; i++) free(S->f[i].v); free(S->f); }
    return ok;
}

static CPath c3_box_path(double x0, double y0, double x1, double y1);
static _Thread_local const char *c3_weave_err;            /* set when wv_loops refuses (the slice is then empty) */
/* Waterline (gen_waterline): the slice weave's fibre ranges (x lo, y lo, x hi, y hi: CWeave +0x198..+0x1b0, FUN_18012be70), its boundary fill (wv_fill) and
   a copy of the weave's loops with their vertices' clipped flags, before the union and the flat-radius offset */
static _Thread_local const double *c3_weave_rng;
static _Thread_local const WvFill *c3_weave_fill;
static _Thread_local CPaths *c3_weave_raw;
static _Thread_local unsigned ***c3_weave_rawfl;   /* the contact words of the loops' vertices (wv_loops) */
/* the slice at tip height z: the forbidden tool-centre region (Clipper polygons, non-zero), within box bx (x0 y0 x1 y1).
   Rb = ball radius of the weave shape (corner radius + ThickZ; 0 = flat tip), zc = its centre, F = the 2D offset put
   back afterwards (VorProfileFlatRad + ThickXY - ThickZ), spacing = MachWeaveResolution. */
static CPaths c3_slice(const C3Mesh *m, double ztip, double Rb, double zc, double F, double spacing, const double *bx, double arc_tol) {
    CPaths raw = { 0 };
    int cap = 0;
    if (Rb <= 0) {   /* flat tip: the parts of the triangles above the tip, projected (h(t) <= 0 skipped, plan 4.5) */
        for (int i = 0; i < m->n; i++) {
            const double (*T)[3] = (const double (*)[3])(m->t + 9 * i);
            if (fmax(T[0][2], fmax(T[1][2], T[2][2])) <= ztip) continue;
            double up[5][3]; int nu = clip_z(T, 3, ztip, 1, up);
            if (nu < 3) continue;
            if (raw.n == cap) { cap = cap ? 2 * cap : 256; raw.p = realloc(raw.p, sizeof(CPath) * cap); }
            double *xy = malloc(sizeof(double) * 2 * nu);
            for (int k = 0; k < nu; k++) { xy[2 * k] = up[k][0]; xy[2 * k + 1] = up[k][1]; }
            raw.p[raw.n].xy = xy; raw.p[raw.n].n = nu;
            if (fabs(cl_area(&raw.p[raw.n])) < 1e-14) { free(xy); continue; }
            if (cl_area(&raw.p[raw.n]) < 0) reverse_path(&raw.p[raw.n]);
            raw.n++;
        }
    } else {
        /* fibres (CWeave FUN_18012b990 / 18012be70, plan 4.5): per axis n = max(3, int((hi - lo) / spacing) + 2),
           t_i = lo + i (hi - lo) / (n - 1), over the box grown by the spacing (+ the shape radius, ours: the region
           the slice can reach) */
        C3Fib *fb[2]; double *pos[2]; int nf[2];
        for (int ax = 0; ax < 2; ax++) {
            double lo = bx[ax ? 0 : 1] - spacing - Rb, hi = bx[ax ? 2 : 3] + spacing + Rb;   /* X fibres lie at y, Y fibres at x */
            if (c3_weave_rng) { lo = c3_weave_rng[ax ? 0 : 1]; hi = c3_weave_rng[ax ? 2 : 3]; }
            nf[ax] = (int)((hi - lo) / spacing) + 2; if (nf[ax] < 3) nf[ax] = 3;
            if (c3_weave_rng) nf[ax]++;   /* Waterline's weave: FUN_180131690 (wd) stores max(3, int((hi - lo) / spacing) + 2) + 1 fibres (the DLL's set +0x48: 272 on MOLD BOTTLE op 4) */
            if (nf[ax] > 200000) nf[ax] = 200000;
            fb[ax] = calloc(nf[ax], sizeof(C3Fib)); pos[ax] = malloc(sizeof(double) * nf[ax]);
            for (int i = 0; i < nf[ax]; i++) {
                if (c3_weave_rng) { double t = (double)i / (double)(nf[ax] - 1); pos[ax][i] = (1.0 - t) * lo + hi * t; }   /* FUN_180131690: (1 - t) lo + t hi, t = i / (n - 1) */
                else pos[ax][i] = lo + i * (hi - lo) / (nf[ax] - 1); }
        }
        for (int i = 0; i < m->n; i++) {
            const double *t = m->t + 9 * i;
            double zmax = fmax(t[2], fmax(t[5], t[8]));
            if (zmax - ztip <= 0) continue;              /* FUN_1801295b0: h = z_max - z_weave <= 0 -> skip */
            for (int ax = 0; ax < 2; ax++) {
                double T[3][3];   /* fibre coordinates: u along the fibre, v across */
                for (int k = 0; k < 3; k++) { T[k][0] = t[3 * k + (ax ? 1 : 0)]; T[k][1] = t[3 * k + (ax ? 0 : 1)]; T[k][2] = t[3 * k + 2]; }
                double vmin = fmin(T[0][1], fmin(T[1][1], T[2][1])) - Rb - 0.003, vmax = fmax(T[0][1], fmax(T[1][1], T[2][1])) + Rb + 0.003;   /* FUN_180130b40: +- r + 0.003 */
                double p0 = pos[ax][0], st = (pos[ax][nf[ax] - 1] - p0) / (nf[ax] - 1);
                int j0 = (int)ceil((vmin - p0) / st), j1 = (int)floor((vmax - p0) / st);
                if (j0 < 0) j0 = 0; if (j1 > nf[ax] - 1) j1 = nf[ax] - 1;
                for (int j = j0; j <= j1; j++) { double lo, hi; c3_ct_on = c3_weave_mc; if (push_tri((const double (*)[3])T, pos[ax][j], zc, Rb, &lo, &hi)) fib_addw(&fb[ax][j], lo, hi, c3_ct[0], c3_ct[1]); c3_ct_on = 0; }
            }
        }
        if (c3_weave_mc) {   /* Waterline: McHmm's loop traversal of the weave (wv_loops above) */
            /* no fib_merge here: wv_loops adds each triangle's interval in turn (the fibre's union FUN_18005fd30 in the drop's order), so each end keeps the contact of
               the triangle that set it */
            c3_weave_err = NULL;
            WvProbe pr = { m, ztip, Rb, zc };
            unsigned **rfl = NULL;
            if (!wv_loops(fb[0], nf[0], pos[0], fb[1], nf[1], pos[1], &pr, c3_weave_resolve, c3_weave_fill, &raw, &rfl, &c3_weave_err)) { for (int i = 0; i < raw.n; i++) { free(raw.p[i].xy); free(rfl[i]); } raw.n = 0; }
            if (c3_weave_fill && raw.n) {   /* the forbidden region of a filled weave reaches the grid's border: its outline as a loop round the free loops (ours: the union below takes the
                                               loops with the non-zero rule; McHmm nests them, FUN_18012b1e0); its vertices count as clipped (the fill's) */
                double fx0 = pos[1][0], fx1 = pos[1][nf[1] - 1], fy0 = pos[0][0], fy1 = pos[0][nf[0] - 1];
                if (raw.n % 64 == 0) { raw.p = realloc(raw.p, sizeof(CPath) * (raw.n + 64)); rfl = realloc(rfl, sizeof(unsigned *) * (raw.n + 64)); }
                raw.p[raw.n] = c3_box_path(fx0, fy0, fx1, fy1); rfl[raw.n] = malloc(sizeof(unsigned) * raw.p[raw.n].n); for (int q_ = 0; q_ < raw.p[raw.n].n; q_++) rfl[raw.n][q_] = WV_CLIP; raw.n++;
                double A = 0; for (int i = 0; i < raw.n - 1; i++) A += cl_area(&raw.p[i]);
                if ((cl_area(&raw.p[raw.n - 1]) > 0) == (A > 0)) reverse_path(&raw.p[raw.n - 1]);   /* the outline runs against the free loops */
            }
            if (c3_weave_raw) { c3_weave_raw->n = raw.n; c3_weave_raw->p = malloc(sizeof(CPath) * (raw.n + 1)); *c3_weave_rawfl = malloc(sizeof(unsigned *) * (raw.n + 1));
                for (int i = 0; i < raw.n; i++) { c3_weave_raw->p[i].n = raw.p[i].n; c3_weave_raw->p[i].xy = malloc(sizeof(double) * 2 * raw.p[i].n); memcpy(c3_weave_raw->p[i].xy, raw.p[i].xy, sizeof(double) * 2 * raw.p[i].n);
                    (*c3_weave_rawfl)[i] = rfl[i]; rfl[i] = NULL; } }
            for (int i = 0; i < raw.n; i++) free(rfl ? rfl[i] : NULL);
            free(rfl);
            if (getenv("MCAM_WV_DEBUG")) { double A = 0;   /* ours (testing) */ for (int i = 0; i < raw.n; i++) A += cl_area(&raw.p[i]); fprintf(stderr, "wv: %d loops, area sum %g\n", raw.n, A); }
        }
        for (int ax = 0; ax < 2; ax++) {
            if (!c3_weave_mc) { for (int j = 0; j < nf[ax]; j++) fib_merge(&fb[ax][j]);
            fibres_quads(fb[ax], nf[ax], pos[ax], ax, &raw, &cap); }
            for (int j = 0; j < nf[ax]; j++) free(fb[ax][j].iv);
            free(fb[ax]); free(pos[ax]);
        }
    }
    CPaths u = raw.n ? cl_bool(&raw, NULL, 0) : (CPaths){ 0 };
    cl_free(&raw);
    if (u.n && fabs(F) > 1e-12) { CPaths o = cl_offset_nz(&u, F, arc_tol); cl_free(&u); u = o; }
    return u;
}

/* --------------------------------------------------------------------------------------------- the op */
static CPath c3_box_path(double x0, double y0, double x1, double y1) {
    CPath c = { malloc(sizeof(double) * 8), 4 };
    double v[8] = { x0, y0, x1, y0, x1, y1, x0, y1 }; memcpy(c.xy, v, sizeof v); return c;
}
/* ------------------------------------------------------------------------------------------ the containment chains (S7) */
/* a stored op's chains (TP kind 6 headers + their kind 7 members, formats/surface_selection.md 2.4) with their roles (+0xe0), in
   chain-number order (mccore tp_get_chains walks the association list by chain number, as cam_fileop.c reads 2D ops). -1 = a chain
   whose geometry is not in the part (cam_chain_from_idns failed). */
static int c3_tp_chains(const McamFile *f, int32_t op_id, Chain **out) {
    *out = NULL; int nch = 0, cap = 0, nid = 0, idcap = 64, role = 0, num = 0, bad = 0;
    int32_t *ids = malloc(sizeof(int32_t) * idcap); int *revs = malloc(sizeof(int) * idcap); int *nums = NULL;
    for (int i = 0; i <= f->ntp; i++) {
        const TpRef *t = i < f->ntp ? &f->tp[i] : NULL;
        if (t && t->op_id != op_id) continue;
        if (!t || t->kind == 6) {
            if (nid) {
                if (nch == cap) { cap = cap ? cap * 2 : 8; *out = realloc(*out, sizeof(Chain) * cap); nums = realloc(nums, sizeof(int) * cap); }
                if (cam_chain_from_idns((McamFile *)f, ids, revs, nid, &(*out)[nch])) { (*out)[nch].role = role; nums[nch++] = num; } else bad = 1;
            }
            nid = 0;
            if (!t) break;
            role = t->role; num = t->chain; continue;
        }
        if (t->kind != 7) continue;
        if (nid == idcap) { idcap *= 2; ids = realloc(ids, sizeof(int32_t) * idcap); revs = realloc(revs, sizeof(int) * idcap); }
        ids[nid] = t->ent; revs[nid] = t->rev == 0 || t->rev == 1 ? t->rev : -1; nid++;
    }
    free(ids); free(revs);
    for (int a = 1; a < nch; a++) for (int b = a; b > 0 && nums[b] < nums[b - 1]; b--) {
        Chain tc = (*out)[b]; (*out)[b] = (*out)[b - 1]; (*out)[b - 1] = tc; int tn = nums[b]; nums[b] = nums[b - 1]; nums[b - 1] = tn; }
    free(nums);
    if (bad) { for (int c = 0; c < nch; c++) cam_chain_free(&(*out)[c]); free(*out); *out = NULL; return -1; }
    return nch;
}
/* the op's chains: its own list, else (a stored op regenerated from its record, p.file_op_id, with no chains given) its TP chains -
   the same rule as the drive references (c3_mesh_op). *own = 1 when the list is allocated here (c3_chains_free). */
static int c3_op_chains(const McamFile *f, CamOp *op, Chain **ch, int *own) {
    *own = 0; *ch = op->chains;
    if (op->nchains || op->p.file_op_id <= 0) return op->nchains;
    int n = c3_tp_chains(f, op->p.file_op_id, ch); if (n > 0) *own = 1;
    return n;
}
static void c3_chains_free(Chain *ch, int n, int own) { if (!own) return; for (int c = 0; c < n; c++) cam_chain_free(&ch[c]); free(ch); }
/* the chains of `type` (0xff = every chain, else its role +0xe0), as mccore number_of_type_chains (decomp/mccore_chain_range.c l.11165: CHAIN +0x4a
   == type, or type 0xff) counts them */
static int c3_count_type_chains(const McamFile *f, CamOp *op, int type) {
    Chain *ch; int own, n = c3_op_chains(f, op, &ch, &own), k = 0;
    for (int c = 0; c < n; c++) if (type == 0xff || ch[c].role == type) k++;
    c3_chains_free(ch, n, own); return n < 0 ? -1 : k;
}
/* The containment as mcmsurf writes it to tmptc.tmp (formats/surface_selection.md 8.2). FUN_1800135a0 (decomp/mcmsurf_all.c l.13325-13337):
   type = 2 (role 2 chains only) for op 132 style 15 (Project: its role 3 chains are the curves), else 0xff (every chain of the op);
   FUN_18012f750 (l.200165) -> FUN_180128390 (l.196075) writes each chain of that type as its linearised points, and with param_6 set
   (every op but 7 / 0xd / 0x6d, FUN_18012f750 l.200203-200209) closes it with its first point (l.196389-196416: the last point is moved
   onto the first, or the first is appended): **an open chain is closed by the straight segment back to its start**. No chain of the
   type: FUN_18012b7c0 removes the files (no containment). The kernel reads each tmptc.tmp chain as a "2D Closed Boundary" (FUN_180081f6c
   mk l.94336 -> FUN_1800ee404). The nested boundaries are united even-odd here (the Boundaries plan's own rule for several boundaries is
   NOT DECODED; ours, as before). Returns 1 (out = the region, empty = no containment) or 0 + err. The tool containment offset is the
   caller's (c3_tcb_offset / sr_containment). */
static int c3_containment(const McamFile *f, CamOp *op, int type, CPaths *out, char *err, size_t errn) {
    memset(out, 0, sizeof *out);
    Chain *ch; int own, n = c3_op_chains(f, op, &ch, &own);
    if (n < 0) { snprintf(err, errn, "the operation's chains cannot be rebuilt (missing geometry)"); return 0; }
    CPaths raw = { 0 }; raw.p = calloc(n ? n : 1, sizeof(CPath));
    for (int c = 0; c < n; c++) {
        if (type != 0xff && ch[c].role != type) continue;
        double *xy, z; int np = cam_chain_polyline(f, &ch[c], op->p.tol, &xy, &z);
        /* FUN_180128390 l.196493-196505: a chain that gives no point fails the op (-0x10) */
        if (np < 1) { free(xy); cl_free(&raw); c3_chains_free(ch, n, own); snprintf(err, errn, "containment chain %d gives no points", c + 1); return 0; }
        raw.p[raw.n].xy = xy; raw.p[raw.n].n = np; raw.n++;   /* the polygon closes itself: the closing segment of l.196389-196416 */
    }
    c3_chains_free(ch, n, own);
    if (raw.n) *out = cl_union_evenodd(&raw);
    cl_free(&raw);
    return 1;
}
/* the HST containment type: 2 for Project (style 15), else 0xff (FUN_1800135a0 l.13329-13332) */
static int c3_hst_tcb_type(const CamOp *op) { return op->p.type == CAM_HSTPROJECT ? 2 : 0xff; }

/* the tool containment offset (mcmsurf FUN_180128390 ms l.196433-196440 -> offsetWriteTcb, decomp/mcmill_all.c l.322700: tmptc.tmp holds the containment already offset, FUN_180081f6c
   reads it as ekBoundaryCollection4): for op 132 d = Offset distance (record 0x12b0) + (Add offset distance to tool radius 0x16c8 ? D / 2 : 0), applied when position 0x16c7 != 1 (Center)
   and |d| > 0.0001; Inside (0) MCCECalcOffset(1, -d), Outside (2) +d (offsetWriteTcb l.190-191 d = -d for 2; FUN_1801d4620 calls MCCECalcOffset(1, -d)). Here Clipper's offset. */
#define C3_TCB_FAIL "Containment offset failed; it may be too large.  Try <center> containment option"   /* mcmsurfres 295 (0x127) */
/* 1 = done; 0 = the offset left nothing: offsetWriteTcb returns 0 < count, FUN_180128390 posts warning 0x127 (ms l.196459-196461) and returns an error, so its caller (ms l.196578, sVar1 == 0 false) makes no toolpath */
static int c3_tcb_offset(CPaths *region, const uint8_t *r, uint32_t n, double D, double arc_tol) {
    int pos = c3b(r, n, 0x16c7), addr = c3b(r, n, 0x16c8); double d = c3d(r, n, 0x12b0) + (addr ? D * 0.5 : 0.0), t = c3d(r, n, 0x12a0);   /* offsetWriteTcb's third argument = op +0x1238 = record 0x12a0 */
    if (pos == 1 || !(fabs(d) > 0.0001) || !region->n) return 1;
    if (pos != 2 && d > 0) {   /* inward by at least half the box's smaller side leaves nothing (Clipper's integers overflow on absurd distances, so this is tested first) */
        double x0 = 1e300, x1 = -1e300, y0 = 1e300, y1 = -1e300;
        for (int i = 0; i < region->n; i++) for (int k = 0; k < region->p[i].n; k++) { double x = region->p[i].xy[2 * k], y = region->p[i].xy[2 * k + 1]; x0 = fmin(x0, x); x1 = fmax(x1, x); y0 = fmin(y0, y); y1 = fmax(y1, y); }
        if (d >= fmin(x1 - x0, y1 - y0) * 0.5) { cl_free(region); region->n = 0; return 0; }
    }
    CPaths o = cl_offset_nz(region, pos == 2 ? d : -d, t > 1e-9 ? t : arc_tol); cl_free(region); *region = o;
    return region->n > 0;
}
int cam3d_debug;   /* ours: MCAM_3D_DEBUG=1 prints the levels / regions */

static int gen_optirough(const McamFile *f, CamOp *op, MB *b) {
    CamParams *p = &op->p;
    if (!cam_op_top(p)) { snprintf(op->error, sizeof op->error, "OptiRough here needs the Top tool plane at the origin"); return 0; }
    uint32_t rn = 0; uint8_t *rec = hst_record(f, p, NULL, &rn);
    if (!rec || rn < 0x1760) { free(rec); snprintf(op->error, sizeof op->error, "no OptiRough record (Mastercam's default op 132 is missing)"); return 0; }
    C3Val v; c3_values(rec, rn, p, mcam_is_metric(f), &v);
    int metric = mcam_is_metric(f);
    McceParm P; memset(&P, 0, sizeof P);
    /* refusals: what is NOT DECODED is not generated (formats/hst3d_port_plan.md 6) */
    if (v.stepup_on) { free(rec); snprintf(op->error, sizeof op->error, "OptiRough Stepup is NOT DECODED here (turn it off)"); return 0; }
    /* K+0x485 (record 0x16ee; flag 0x2000000 below): FUN_180106624 (mk l.203234-203236) also grows the job's containment +0xe8 by D / 2 (FUN_180107e34, MCCECalcOffset(1, D / 2)) and hands it to
       MCCECalcDynPock as its second boundary list (FUN_1800f7330 mk l.190834, param_4); here the engine runs without that list, so the option is refused (0 in every default op and the stored ops) */
    if (v.b485) { free(rec); snprintf(op->error, sizeof op->error, "OptiRough: record 0x16ee (K+0x485, the containment handed to the Dynamic engine) is NOT PORTED"); return 0; }
    /* Core (K+0x486 = record 0x16ef): FUN_180108f1c mk l.205380 / l.205457 give the engine flag 0x40 (for 0x10) and approach 0.65 D, but its ConstraintBoundaries are the surfaces' Bounding Box plan (mk l.93448 "ekPlan3, ekPlan2")
       and the executioner's region for Core is NOT DECODED, so v.core is forced to 0 below: the flag 0x40 and the approach CANNOT run (dead code, kept as the decode), the op is cut as Pocket's region walls, with a warning (decision B6-1's stand-in rule). Measured: TUTORIAL 7 inch op 5 (Core) with Use Z depths: Pocket walls p95 0.043 / 0.043 in, cut length 766 vs 778,
       floor 45 / 45; the Core flag on the same region: p95 0.090 / 0.400, length 463, floor 57 (formats/audit_2026-11-09_highspeed_surface1.md). */
    int core_fallback = v.core; v.core = 0;
    if (!(v.stepdown > 0) || !(v.stepover > 0) || !(v.D > 0)) { free(rec); snprintf(op->error, sizeof op->error, "stepdown, stepover and tool diameter must be > 0"); return 0; }
    double R = v.D * 0.5, Rc = fmin(fmax(v.Rc, 0.0), R);

    /* CDynPockParm (FUN_180108f1c, mk l.205368) */
    P.flags = v.core ? 0x40 : 0x10; P.flags |= 2;                       /* (K+0x486 != 0 ? 0x30 : 0) + 0x10, | 2 (mk l.205380-205384) */
    P.order_mode = v.stepdown_order == 0 ? 1 : v.stepdown_order == 1 ? 2 : 0;   /* CDynPockParm +0x20 from K+0x4ea (record 0x1660): 0 -> 1, 1 -> 2, else 0 (mk l.205385-205391) */
    
    if (!v.conventional) P.flags |= 1;                                    /* K+0x47c == 0 -> | 1 (mk l.205392; K+0x47c = record 0x16e4 = 1 for Conventional, HSTOptRoughParamPage UpdateLocalOp l.96657-96664); flags & 1 = climb in the engine (mcce_dyn.c climb) */
    if (v.zigzag) P.flags |= 5;                                           /* K+0x487 != 0 -> | 5 (mk l.205395; K+0x487 = record 0x1728 by FUN_180923f60, the cut method combo's Zigzag, l.96662) */
    switch (v.entry) { case 1: P.flags |= 0x200; break; case 2: P.flags |= 0x400; break; case 3: P.flags |= 0x800; break; case 4: P.flags |= 0x1000; break; default: P.flags |= 0x100; }
    if (!v.b480) P.flags |= 0x2000;
    if (metric) P.flags |= 8;
    switch (v.retract_mode) { case 1: P.flags |= 0x10000; break; case 2: P.flags |= 0x40000; break; case 3: P.flags |= 0x80000; break; case 4: P.flags |= 0x20000; break; }
    if (v.b481) P.flags |= 0x40000000;
    P.R = R; P.r = Rc;
    { double x = 1.0 - v.stepover / R; if (x < -1) x = -1; if (x > 1) x = 1; P.eng = acos(x); if (P.eng > 2.9670597) P.eng = 2.9670597283903604; }   /* FUN_1801065f0, cap 0x4007bc89cbc63c90 */
    P.zclear = v.zclear; P.helix_r = v.helix_r; P.pitch = v.pitch; P.plunge_ang = v.plunge;
    P.troch_r = v.troch_r; P.slot_add = v.slot_w; P.min_rad = v.min_rad;
    P.gap = v.gap_pct ? v.gap_p * v.D * 0.01 : v.gap_d;                  /* TP+0x280 = 0x1308 (with the % radio: % x D, as LoadDynPockParm 2D, mcce_dyn.c l.724) */
    P.micro_lift = v.micro_lift;
    if (v.core) P.approach = v.D * 0.65;
    P.chain_tol = cam_chain_tol(metric);                                  /* +0x110 = job +0x20 (the chaining tolerance, as 2D: cam2d.h) */
    P.sliver = P.chain_tol * 10.0 * 2.0;                                  /* +0x58 = +0x110 x 10 x 2 */
    /* feeds: mcce's feed fields (mcce_parm_from_record's meaning) from the op's feeds - ours where OptiRough's are not mapped */
    P.f_cut = p->feed; P.f_back = v.back_feed > 1e-5 ? v.back_feed : p->feed; P.f_ramp = v.ramp_feed > 1e-5 ? v.ramp_feed : p->feed;
    P.f_first = p->feed; P.f_conv = v.conv_feed > 1e-5 ? v.conv_feed : p->feed; P.f_retract = p->feed;   /* conventional < 1e-5: the op feed (MapToDlg
                                                                          FUN_180091f20 shows 0x2400 = the feed then) */
    if (v.entry != 0) { free(rec); snprintf(op->error, sizeof op->error, "OptiRough entry method %d (medial): the medial-axis entries are NOT DECODED here (decision B9-3)", v.entry); return 0; }
    double tol = CAM_SYS_TOL;                                             /* LoadDynPockParm's floors for these (mcce_dyn.c l.744-746) */
    if (P.plunge_ang <= tol) P.plunge_ang = 0.026179938779914945;
    if (P.pitch <= tol) P.pitch = metric ? 2.54 : 0.1;
    if (P.troch_r <= tol) P.troch_r = metric ? 2.54 : 0.1;
    free(rec);

    /* the drive mesh: deflection = msurf_chord_dev = record 0x12a0 (msurf_map l.340012: op +0x1238 for op 132; mcmsurf hands it to the
       drive tessellation, FUN_1803b94d0(.., msurf_chord_dev) before run_mchardmill_macro, l.18258). The tessellator is ours (OCCT);
       a record without it (0): a tenth of the cut tolerance (ours) */
    C3Mesh M; char err[150];
    if (!c3_mesh_op(f, op, v.ztol > 1e-9 ? v.ztol : v.tol * 0.1, &M, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); return 0; }
    /* PosZHi / PosZLo = job +0x40 / +0x38 (FUN_180071314 mk l.87321-87348), set by FUN_18007bb48 (mk l.91572-91810, called from
       the job setup with the job flag +0x50 = 1, FUN_18006dd68 l.84693):
       - start: hstJobIdDat +0x28 / +0x30 (job +0x20 + 0x28 / 0x30, copied by mccore FUN_1809251c0 = decomp/poszlo/setup.c), which
         mcmsurf's process_3rd_party_algorithm FUN_1800199d0 (mcmsurf_all.c l.18230) fills with FUN_180258310 (l.395462): the lowest
         / highest vertex Z of the drive's tessellation, as written to tmphardmillraw.stl. Here: the box of our mesh (the
         tessellation itself is ours, see c3_mesh_op);
       - op 132: a floor stock ToolThicknessZ (TP+0x320) > 0 raises PosZHi by it, < 0 lowers PosZLo by it; then PosZHi += TP+0x220,
         PosZLo -= TP+0x220 (l.91783-91790);
       - styles in the mask 0x5b6 also lower PosZLo by the corner radius (TP+0x70 +0x10 = GetHstCornerRadius); OptiRough is kernel
         style 9 (plan 4.2), not in the mask.
       TP+0x220 = record 0x12a0 [inferred]: the same "x 0.01" tolerance use for op 132 as mcmsurf's op +0x1238 (= record 0x12a0) in
       FUN_180258310 l.395670, the default 0.001 / 0.025 when 0 (FUN_18006dd68, FUN_18006f2e8) and 0.025 in the corpus. Check: TUTORIAL 7
       op 5 (drive top 0, floor stock 1, 0x12a0 = 0.025) gives Mastercam's first level 1.025 (plan 6.3).
       NOT PORTED (NOT DECODED record offsets): the stock model's Z range (FUN_18007d7c8 l.92719, TP+0x463), the depth limits
       (TP+0x464, + job +0x28), the rest stock range (FUN_18006f158). A stock below -(corner radius + tolerance) takes another path
       (FUN_1800f1860, l.91648: NOT DECODED, refused). */
    double ztol = v.ztol;
    if (fmin(v.sxy, v.sz) + Rc < -ztol) {
        snprintf(op->error, sizeof op->error, "negative stock to leave below the corner radius: NOT DECODED"); c3_mesh_free(&M); return 0;
    }
    double zhi = M.hi[2], zlo = M.lo[2];
    if (v.sz > 0.0) zhi += v.sz; else if (v.sz < 0.0) zlo += v.sz;
    zhi += ztol; zlo -= ztol;
    {   /* Use Z depths: K+0x464 = record 0x16c9, K+0x1c0 / 0x1c8 = record 0x1238 / 0x1240 (FUN_180923f60): PosZHi / PosZLo = max / min of them + job +0x28 (the tool plane origin's Z: 0 here, Top at the origin), mk l.87321-87344 */
        uint32_t rn2 = 0; uint8_t *r2 = hst_record(f, p, NULL, &rn2);
        if (r2) { if (c3b(r2, rn2, 0x16c9)) { double d1 = c3d(r2, rn2, 0x1238), d2 = c3d(r2, rn2, 0x1240); zhi = fmax(d1, d2); zlo = fmin(d1, d2); } free(r2); }
    }
    if (getenv("MCAM_3D_ZHI")) zhi = atof(getenv("MCAM_3D_ZHI"));   /* ours (testing): PosZHi / PosZLo given */
    if (getenv("MCAM_3D_ZLO")) zlo = atof(getenv("MCAM_3D_ZLO"));
    /* the weave shape (plan 4.5) */
    double sz = v.sz, sxy = fmax(v.sxy, v.sz);   /* ToolThicknessXY = max(XY, Z stock): the branch of every style but Horizontal (3), FUN_180071314 mk l.87577-87611 */
    int flat_tip = Rc + sz <= 0.0;
    double Rb = flat_tip ? 0.0 : Rc + sz, Fo = flat_tip ? R + sxy : (R - Rc) + sxy - sz;
    double spacing = c3_resolution(R, Rc, sz, v.tol);
    if (!(spacing > 0)) spacing = R;
    double arc_tol = fmax(v.tol * 0.5, 1e-4);
    /* the containment: the op's chains as tmptc.tmp holds them (c3_containment, type 0xff) else the model's XY box (the Bounding Box Plan 0x1425
       gets no OffsetDistance for kernel style 9: mk l.92849-92858, mask 0x209, local_24f set only for Area Roughing's Core; the plan's
       default distance is NOT DECODED, 0 here - ours, as before) */
    CPaths region = { 0 };
    if (!c3_containment(f, op, c3_hst_tcb_type(op), &region, err, sizeof err)) { snprintf(op->error, sizeof op->error, "%s", err); c3_mesh_free(&M); return 0; }
    { uint32_t rn2 = 0; uint8_t *r2 = hst_record(f, p, NULL, &rn2); if (r2) { int ok = c3_tcb_offset(&region, r2, rn2, v.D, arc_tol); free(r2); if (!ok) { snprintf(op->error, sizeof op->error, C3_TCB_FAIL); cl_free(&region); c3_mesh_free(&M); return 0; } } }   /* the containment as written to tmptc.tmp */
    if (!region.n) { region.p = calloc(1, sizeof(CPath)); region.p[0] = c3_box_path(M.lo[0], M.lo[1], M.hi[0], M.hi[1]); region.n = 1; }
    double bx[4] = { 1e300, 1e300, -1e300, -1e300 };
    for (int i = 0; i < region.n; i++) for (int k = 0; k < region.p[i].n; k++) { bx[0] = fmin(bx[0], region.p[i].xy[2 * k]); bx[1] = fmin(bx[1], region.p[i].xy[2 * k + 1]); bx[2] = fmax(bx[2], region.p[i].xy[2 * k]); bx[3] = fmax(bx[3], region.p[i].xy[2 * k + 1]); }
    /* the levels: OptiRough's StepDownEvenLastSteps 1 (mk l.88404). Avoidance: FUN_180109910 does not set it (the 0.0001 at mk l.205595 belongs to FUN_18010921c, the Stock Model macro with plan 0x1436), so plan 0x1478 keeps the default 0x1bbf = 0.5 x 0x1bbe (hp l.533-577) */
    double *zl = NULL; int nz = c3_levels_infl(&M, bx, sz, zhi, zlo, v.stepdown, 1, -1, metric ? 1.0 : 1.0 / 25.4, &zl);
    if (nz < 0) { snprintf(op->error, sizeof op->error, nz == -1 ? "cancelled" : "OptiRough: the Z levels do not descend (the stepdown is below the numeric precision of the Z range)"); free(zl); cl_free(&region); c3_mesh_free(&M); return 0; }
    if (getenv("MCAM_3D_LEVELS")) { for (int k = 0; k < nz; k++) fprintf(stderr, "level %.4f\n", zl[k]); }   /* ours (testing) */
    if (getenv("MCAM_3D_DEBUG")) cam3d_debug = 1;
    if (cam3d_debug) { fprintf(stderr, "optirough: %d tris, xy %.4f %.4f .. %.4f %.4f, z %.4f..%.4f, %d levels, R %.4f Rc %.4f Rb %.4f F %.4f spacing %.4f\n", M.n, M.lo[0], M.lo[1], M.hi[0], M.hi[1], zlo, zhi, nz, R, Rc, Rb, Fo, spacing);
        fprintf(stderr, "optirough: containment %d loops, box %.4f %.4f .. %.4f %.4f, tool position %d offset %.4f add R %d\n", region.n, bx[0], bx[1], bx[2], bx[3], v.cont_pos, v.cont_off, v.cont_add_r); }

    double fp = p->top_z + p->feed_plane;                                 /* the feed plane (SAFE links) */
    if (fp < zhi + p->feed_plane) fp = zhi + p->feed_plane;
    int down = 0, okall = 1, nregs = 0, nleft = 0;
    /* the first level is PosZHi itself (FUN_18040da70 +0x2b9): at the model top with the stock = the model's box there is
       nothing above it to cut; it is cut only when the op's top of stock is above it (ours) */
    for (int k = 0; k < nz && okall; k++) {
        double z = zl[k];
        if (k == 0 && !(p->top_z > z + 1e-9)) continue;
        if (mcce_abort_get()) { snprintf(op->error, sizeof op->error, "cancelled"); okall = 0; break; }
        clock_t c0 = clock();
        CPaths forb = c3_slice(&M, z - sz, Rb, z + Rc, Fo, spacing, bx, arc_tol);   /* the weave Z = level - c, c = Eva +0x140 = ToolThicknessZ (FUN_1802f4c80): FUN_18040cf60 hz l.13009, FUN_180413d90 hz l.16662, set at CWeave +0x158 by FUN_1801298a0; a triangle counts when h = zmax - z_weave > 0 (FUN_1801295b0) */
        if (cam3d_debug) fprintf(stderr, "  level %.4f: slice %.2f s\n", z, (double)(clock() - c0) / CLOCKS_PER_SEC);
        CPaths fre = forb.n ? cl_bool(&region, &forb, 2) : cl_bool(&region, NULL, 0);
        if (cam3d_debug) fprintf(stderr, "  level %.4f: forbidden %d loops area %.3f, free %d loops area %.3f\n", z, forb.n, cl_total_area(&forb), fre.n, cl_total_area(&fre));
        if (getenv("MCAM_3D_DUMP")) {   /* ours (testing): the slice and the free region of each level, "L z" / "F n" / "R n" + x y lines */
            FILE *df = fopen(getenv("MCAM_3D_DUMP"), k ? "a" : "w");
            if (df) { fprintf(df, "L %.6f\n", z);
                for (int i = 0; i < forb.n; i++) { fprintf(df, "F %d\n", forb.p[i].n); for (int q = 0; q < forb.p[i].n; q++) fprintf(df, "%.6f %.6f\n", forb.p[i].xy[2 * q], forb.p[i].xy[2 * q + 1]); }
                for (int i = 0; i < fre.n; i++) { fprintf(df, "R %d\n", fre.p[i].n); for (int q = 0; q < fre.p[i].n; q++) fprintf(df, "%.6f %.6f\n", fre.p[i].xy[2 * q], fre.p[i].xy[2 * q + 1]); }
                fclose(df); }
        }
        cl_free(&forb);
        /* the region boundaries as mcce pocket walls: offset by the tool radius (FUN_180107dcc(R), see the head) */
        CPaths wall = fre.n ? cl_offset_nz(&fre, R, arc_tol) : (CPaths){ 0 };
        cl_free(&fre);
        int *own = NULL; CPaths g = wall.n ? cl_group(&wall, &own) : (CPaths){ 0 };
        cl_free(&wall);
        McceBnd *bd = calloc(g.n + 1, sizeof(McceBnd));
        for (int i = 0; i < g.n && okall; i++) {
            if (own[i] != i) continue;
            int nb = 0;
            for (int j = 0; j < g.n; j++) if (own[j] == i) { bd[nb].xy = g.p[j].xy; bd[nb].n = g.p[j].n; bd[nb].island = j != i; bd[nb].open = 0; nb++; }
            P.depth = z; P.top = k > 0 ? zl[k - 1] : p->top_z;
            McceRec *r = NULL; int nr = 0; char e2[160] = "";
            if (!mcce_calc_dyn_pock(&P, bd, nb, NULL, 0, NULL, 0, &r, &nr, e2, sizeof e2)) {
                if (cam3d_debug) fprintf(stderr, "  level %.4f region %d: mcce failed (%s)\n", z, i, e2);
                free(r); nleft++; continue;   /* a region the engine cannot take is left (the warning below) */
            }
            if (nr == 0) nleft++;             /* MCCECalcDynPock made no motion (e.g. no entry fits): the region stays uncut */
            if (cam3d_debug) fprintf(stderr, "  level %.4f region %d: %d bounds, %d records (%.2f s)\n", z, i, nb, nr, (double)(clock() - c0) / CLOCKS_PER_SEC);
            if (getenv("MCAM_3D_RECS")) for (int q = 0; q < nr && q < atoi(getenv("MCAM_3D_RECS")); q++)   /* ours (testing) */
                fprintf(stderr, "    rec %d %c ce %02x mf %x z %.4f..%.4f (%.3f,%.3f)-(%.3f,%.3f) feed %.1f\n", q, r[q].id, r[q].ce, r[q].mf, r[q].z0, r[q].z1,
                        r[q].id == 'A' ? r[q].cx + r[q].r * cos(r[q].sa) : r[q].x1, r[q].id == 'A' ? r[q].cy + r[q].r * sin(r[q].sa) : r[q].y1,
                        r[q].id == 'A' ? r[q].cx + r[q].r * cos(r[q].sa + r[q].sw) : r[q].x2, r[q].id == 'A' ? r[q].cy + r[q].r * sin(r[q].sa + r[q].sw) : r[q].y2, r[q].feed);
            /* the first record's start Z: mcce's output loop starts it from depth + 1 (the engine's previous-position Z, FUN_180012390
               l.15564-15617, not a motion); Mastercam writes every record to its own XYZ (OutputMotionToBnci, decomp/mcmill_hst2d.c
               l.4445 / 4534-4540) - formats/qt_port_plan.md "B9-5" item 1. A new op type: the decoded rule applies here (2D keeps its
               gated behaviour) */
            int r0 = 0;
            if (nr > 0) { r[0].z0 = r[0].z1;
                if (r[0].id == 'L' && nr > 1 && fabs(r[0].x1 - r[0].x2) < 1e-12 && fabs(r[0].y1 - r[0].y2) < 1e-12) r0 = 1; }   /* then a point: no move */
            mcce_moves(b, p, r + r0, nr - r0, fp, &down);
            if (down) { V3 l = lastp(b); mv(b, 1, l.x, l.y, fp, 0); down = 0; }
            free(r); nregs++;
        }
        free(bd); cl_free(&g); free(own);
    }
    cl_free(&region); c3_mesh_free(&M); free(zl);
    if (!okall) return 0;
    if (!b->n) { snprintf(op->error, sizeof op->error, "nothing to cut (%d levels, %d regions)", nz, nregs); return 0; }
    { V3 e = lastp(b); mv(b, 1, e.x, e.y, p->clearance, 0); }
    if (core_fallback) snprintf(op->warning, sizeof op->warning, "OptiRough Core (from outside) is NOT DECODED: cut as Pocket (the region walls); stand-in, ours");
    else if (nleft) snprintf(op->warning, sizeof op->warning, "%d region(s) were not cut: the Dynamic engine made no motion there (no entry fits)", nleft);
    else if (v.clamped) snprintf(op->warning, sizeof op->warning, "The requested stepover exceeds the flat diameter of the selected tool, the stepover will be set "
                                 "to the max allowable stepover of 95%% of the selected tool's flat diameter.");   /* mcmsurfres 500 */
    return 1;
}
