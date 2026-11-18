/* Toolpaths manager item texts (included by mcamui.c after the camops table).
 *
 * Sources (mastercam.dll unless noted; decomp/opman_tree_all.c, decomp/mccore_descriptions.c):
 *  - op row: COpMgrTree::InitTvOpText @1800c3350 with the OpMgrDisplay defaults FUN_180343d50 (every flag 1 except the
 *    op ID, +0x75a8 = 0; no OpMgrDisplay file ships): "%ld" (position among the tree's ops, GetTvOpText), " - " +
 *    op_make_description(op, 0) (mccore @1800c7720), unless op_has_no_view: " - [" + opmanres 3058 "WCS" + ": " + plane
 *    name ("?" when empty) + "]" and the same with 3059 "Tplane", then " - " + comment (op +6) when not empty.
 *  - op_make_description: name id FUN_1800ca8a0(opcode) (= formats/opcodes.tsv / mcam_opcode_name); contour (1) adds
 *    " (" + FUN_1800b0010(op+0x12a0 + 10) + ")" = mccoreres 4561 2D / 4563 3D / 4565 2D chamfer / 4567 3D chamfer /
 *    4569 Ramp / 4571 Remachining; pocket (3) " (" + FUN_1800cb1c0(op+0x1392 + 10) + ")" = 4792 Standard / 4794 Facing
 *    / 4796 Island facing / 4798 Remachining / 4800 Open; drill (op_is_drill, not 0x37-0x39 / 0x132) = the cycle text
 *    FUN_1800b0530: post text page 0x27 item cycle*12+1 (not read here: cycles 0-7 use mccoreres 7645 + 11 n, see drill_cycle_text; the fallback is
 *    mccoreres 4877 + cycle for cycles 0..19, 5218 for 30, else 4974; drill_cycle_text). Operation struct offset X = OP_ID
 *    record offset X + 0x68 (opcode +4 -> 0x6c, comment +6 -> 0x6e).
 *  - Parameters row (InitTvItem case -3): opmanres 3043 (+ " - " 3044 "Work offset" " #%hd" when op+0x602 != -1).
 *  - tool row (case -4): mccore Cnc::Tool::tl_make_description @1800db8a0, upper-cased; the tree splits it at the first
 *    '-' and joins the trimmed halves with " - " (both halves shown by default) = the same text.
 *  - Geometry row (case -5): opmanres 3047 + " - " + " (%ld) chain(s)" (3045, op+0x8c) for op_is_chained and not
 *    op_is_msurf ops, " (%ld) point(s)" (3046, op+0xd4) for op_is_point ops (mccore decompiles in
 *    decomp/mccore_descriptions.c, decomp/mccore_toolfilt.c).
 *  - Toolpath row (case -6): opmanres 27625 "Toolpath" + " - " + "%.1lfK" of the NCI size (op+0x84 - op+0x80) / 1024,
 *    then " - " + NC file name + NCSuffix (control definition text 0x15: NOT DECODED, left out), then " - %s %ld" with
 *    opmanres 161 "Program #" and op+0xf8.
 */

static int otx_s16(const Op *o, int off) { return o->raw && off + 2 <= (int)o->raw_n ? (int16_t)(o->raw[off] | o->raw[off + 1] << 8) : 0; }
static int otx_s32(const Op *o, int off) { int32_t v = 0; if (o->raw && off + 4 <= (int)o->raw_n) memcpy(&v, o->raw + off, 4); return v; }
static CamOp *otx_camop(int i) { for (int k = 0; k < ncamops; k++) if (camops[k]->op_index == i) return camops[k]; return NULL; }
static int otx_opcode(int i) {
    const Op *o = &F.ops[i]; if (o->raw) return o->opcode;
    CamOp *c = otx_camop(i); int st; return c ? mcam_opcode_for(&c->p, &st) : 0;
}
static int otx_is_drill(int oc) { return oc == 2 || oc == 0x1c || (oc >= 0x37 && oc <= 0x39) || oc == 0x132; }   /* feeds_speeds.md */
static int otx_is_point(int oc) { return otx_is_drill(oc) || oc == 0x12 || oc == 100 || oc == 106 || oc == 113; }  /* mccore op_is_point */
static int otx_is_msurf(int oc) {                          /* mccore op_is_msurf @1800c62b0 */
    static const short s[] = { 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0x1c, 0x1d, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x30, 0x6b, 0x6d, 0x6e, 0x70, 0x71, 0x83, 0x84 };
    for (size_t k = 0; k < sizeof s / sizeof *s; k++) if (s[k] == oc) return 1;
    return 0;
}
static int otx_is_contour(int oc) { return oc == 1 || (oc >= 0x33 && oc <= 0x35) || oc == 0x4a || oc == 0x53 || oc == 0x82; }   /* feeds_speeds.md op_is_contour (listed part) */
static int otx_is_chained(int oc) {                        /* mccore op_is_chained @1800c5ca0 (its first test, an op class
                                                              call Ghidra did not name, is NOT DECODED) */
    if (otx_is_contour(oc) || oc == 3 || oc == 0x66 || otx_is_msurf(oc)) return 1;
    unsigned a = (unsigned short)(oc - 0x4a), b = (unsigned short)(oc - 0x15), c = (unsigned short)(oc - 0xc9);
    if (a <= 0x3d && (0x30000074a00002a5ULL >> a & 1)) return 1;
    if (b <= 0x33 && (0xc81800800017bULL >> b & 1)) return 1;
    if (c <= 0x17 && (0x800623U >> c & 1)) return 1;
    return oc == 0x1b7;
}
static const char *drill_cycle_text(int cyc) {             /* FUN_1800b0530: post text page 0x27 item cycle*12+1, else
                                                              mccoreres 0x130d + cycle (0..19), 5218 for 30, else 4974 */
    /* the post text is not read here; cycles 0-7 take the texts the op dialog's Cycle combo uses (mccoreres 7645 + 11 n,
       opparams_ui.inc) - an inference, the CPostText default page is NOT DECODED */
    static const char *t[8] = { "Drill/Counterbore", "Peck Drill", "Chip Break", "Tap", "Bore #1", "Bore #2", "Misc. #1", "Misc. #2" };
    static char b[40];
    if (cyc >= 0 && cyc < 8) return t[cyc];
    if (cyc >= 8 && cyc < 20) { snprintf(b, sizeof b, "Custom drill cycle #%d", cyc + 1); return b; }   /* 4885-4896 */
    return cyc == 30 ? "5 axis" : "UNDEFINED";
}

static void otx_desc(int i, char *out, size_t n) {         /* op_make_description(op, 0, ...) */
    const Op *o = &F.ops[i]; CamOp *c = otx_camop(i);
    int oc = otx_opcode(i);
    const char *name = oc ? mcam_opcode_name(oc) : o->type;   /* new op types we cannot export have no opcode: our label */
    if (otx_is_drill(oc) && oc != 0x132 && !(oc >= 0x37 && oc <= 0x39)) {
        int cyc = o->raw ? otx_s16(o, 0x1228) : c ? c->p.drill_cycle : 0;
        snprintf(out, n, "%s", drill_cycle_text(cyc)); return;
    }
    if (oc == 1) {
        static const char *ct[] = { "2D", "3D", "2D chamfer", "3D chamfer", "Ramp", "Remachining" };
        int t = o->raw ? otx_s16(o, 0x1308) : c && c->p.contour_type == 1 ? 2 : 0;
        if (t >= 0 && t <= 5) { snprintf(out, n, "%s (%s)", name, ct[t]); return; }
    }
    if (oc == 3) {
        static const char *pt[] = { "Standard", "Facing", "Island facing", "Remachining", "Open" };
        int t = o->raw ? otx_s16(o, 0x13fa) : 0;
        if (t >= 0 && t <= 4) { snprintf(out, n, "%s (%s)", name, pt[t]); return; }
    }
    snprintf(out, n, "%s", name);
}
static const char *SYS_PLANES[7];
static const char *otx_plane(int id) { return id >= 1 && id <= 7 ? SYS_PLANES[id - 1] : "?"; }   /* the part's SystemPlanes
                                       names (levels_ui.inc); user planes (ids > 7) are not read: "?" */
static void otx_op_row(int i, char *out, size_t n) {
    const Op *o = &F.ops[i]; char d[128]; otx_desc(i, d, sizeof d);
    /* InitTvOpText: GetPlanePtrByID(op +0x738) / (op +0x5d8) = record 0x7a0 / 0x640, the op's WCS / Tplane op_view plane ids.
       An op made or taken over here (no record; a regenerated stored op keeps its operation and so its views in Mastercam):
       the ids its record gets on save (cam_planes_to_record: CamParams wcs / tplane, 0 = Top), the stored op's own Tplane id
       when tplane is only its matrix's system plane (tplane_rec_id, Lathe Face). Before 2026-11-16 (L-4) such an op showed Top. */
    CamOp *c = o->raw ? NULL : otx_camop(i);
    int wcs = o->raw ? otx_s16(o, 0x7a0) : c && c->p.wcs > 0 ? c->p.wcs : 1;
    int tpl = o->raw ? otx_s16(o, 0x640) : c && c->p.tplane_rec_id > 0 ? c->p.tplane_rec_id : c && c->p.tplane > 0 ? c->p.tplane : 1;
    snprintf(out, n, "%d - %s - [WCS: %s] - [Tplane: %s]%s%s", i + 1, d, otx_plane(wcs), otx_plane(tpl), o->comment[0] ? " - " : "", o->comment);
}
static void otx_params_row(int i, char *out, size_t n) {
    const Op *o = &F.ops[i]; int wo = o->raw ? otx_s16(o, 0x66a) : -1;
    if (wo != -1) snprintf(out, n, "Parameters - Work offset #%d", wo); else snprintf(out, n, "Parameters");
}
static void tm_describe(const Tool *t, char *o, size_t n, int upper);   /* toolmgr_ui.inc: tl_make_description (upper) /
                                                                        TpToolList::CreateToolDescription (not) */
static void otx_tool_desc(const Tool *t, char *out, size_t n, int upper) { tm_describe(t, out, n, upper); }
static void otx_tool_row(int i, char *out, size_t n) {
    const Tool *t = mcam_tool(&F, F.ops[i].tool);
    if (t) otx_tool_desc(t, out, n, 1); else snprintf(out, n, "Tool does not exist!");   /* opmanres 158 (0x9e) */
}
static void otx_geom_row(int i, char *out, size_t n) {
    const Op *o = &F.ops[i]; CamOp *c = otx_camop(i); int oc = otx_opcode(i);
    if (otx_is_chained(oc) && !otx_is_msurf(oc)) snprintf(out, n, "Geometry -  (%d) chain(s)", o->raw ? otx_s32(o, 0xf4) : c ? c->nchains : 0);
    else if (otx_is_point(oc)) snprintf(out, n, "Geometry -  (%d) point(s)", o->raw ? otx_s32(o, 0x13c) : c ? c->nholes : 0);
    else snprintf(out, n, "Geometry - ");
}
static void otx_path_row(int i, char *out, size_t n) {
    const Op *o = &F.ops[i];
    int a = otx_s32(o, 0xe8), b = otx_s32(o, 0xec);
    double k = o->raw && b > a ? (b - a) / 1024.0 : 0;     /* new ops: no NCI written here -> 0.0K */
    snprintf(out, n, "Toolpath - %.1fK - Program # %d", k, o->raw ? otx_s32(o, 0x160) : 0);
}

/* Doc file (COpMgrTree::DocFile @180378c70 -> DumpItemArray @1800be160 -> CStringArrayToFile @180440760): line 1 = part
   path + "    " + time "%A, %B %d, %Y   %H:%M:%S", line 2 " " (each line TrimRight), then every tree item (expanded or
   not) indented by 4 spaces per depth; written to the temp directory as "<part name> Operations Manager" + txt suffix
   and opened in the editor (edit_filename). Here: $TMPDIR (else /tmp), opened with xdg-open outside scripted runs. */
static void otx_line(FILE *o, int depth, const char *s) {
    char b[600]; int k = 0;
    for (int d = 0; d < depth && k < 500; d++) { memcpy(b + k, "    ", 4); k += 4; }
    snprintf(b + k, sizeof b - k, "%s", s);
    size_t L = strlen(b); while (L && (b[L - 1] == ' ' || b[L - 1] == '\t')) b[--L] = 0;
    fprintf(o, "%s\n", b);
}
