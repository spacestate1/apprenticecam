/* cui_opdlg.c - the model of the operation parameter dialog (mcmill ToolpathDlg, mccoreres dialog 6544): the page tree
   rules, the Linking page's height flags and rows, the pages' pictures, tree images, toolbar, quick view, title and every
   page's controls (pp_page_body, walked through the front end's PpOps), OK / Cancel. Moved from viewer/opparams_ui.inc
   (formats/qt_port_plan.md phase 6; formats/mcamui_dialogs.md 1 is the code map); names, comments and citations kept.
   The front ends draw it: mcamui opparams_ui.inc (nuklear), viewer-qt opdlg.cpp (Qt). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "cui_opfields.h"
#include "cam_fileop.h"
#include "mcam_saveops.h"
#include "nc_post.h"
#include "cam_postvals.h"
static int pf_dlg(const CamParams *p, int s);
int pv_qv_coolant(void);

/* The operation parameter dialog (mcmill ToolpathDlg) - included by mcamui.c.
 *
 * Sources (decompiles in ../decomp/mcmill_all.c and ../decomp/mccore_tpdlg.c, ../decomp/mccore_tptree.c):
 *  - title (FUN_18026dc70): mcmillres 2755 "2D Toolpaths" (opcodes 1, 3, 0x66, 0x69, 0x82, 0x85) or 2707 "2D High Speed
 *    Toolpath" (0x86) + " - " (DAT_18031f77c) + GetToolpathTypeName: ChainBasedToolpathDlg FUN_18026d220 (mccoreres 5015
 *    Contour, 5016 Pocket, 5017 Facing, 5018 Slot Mill), PointBasedToolpathDlg FUN_180270050 (4343 Circle Mill, 4394
 *    Thread Mill, 4397 Helix Bore; drill: mcmillres 2756 "Drill/Circles" + " " + the post's cycle text page 45 - here
 *    the cycle names mccoreres 7645 + 11 n, the post text is NOT read), HST FUN_18026ea30 (mcmillres 2777 Dynamic Mill,
 *    2773 Area Mill, 2818 Dynamic Contour, 2703 Peel Mill, 2713 Blend Mill by style +0x32b0).
 *  - pages: slots 1..35 created by MakeSpecificPages (ChainBasedToolpathDlg FUN_18026d480, PointBasedToolpathDlg
 *    FUN_1802701e0, HST2DChainBasedToolpathDlg FUN_18026eb40) through the page factories FUN_18026c800 / FUN_18026f760
 *    / FUN_18026e210 (NULL = no page for this opcode); the tree = ToolpathTree builder FUN_18014a700 (titles from
 *    mccoreres / mcmillres, parent / child order as inserted). Product bits: Home / Ref. Points, Planes, Coolant, Canned
 *    Text, Misc Values and the axis pages exist unless GetProdEnableBits() & 0x100 (Mill-Turn); Multiple heads only
 *    with & 0x40 - taken as not set.
 *    The Tab Cutoff op (opcode 0x82, CamParams.ct_cutoff_op): the contour's slots except Tabs 0x19 / Tab Cutoff 0x1e
 *    (FUN_18026d480 l.424900 adds them only for opcode 1; FUN_18026c800 cases 0x19 / 0x1e return NULL for 0x82); its Toolpath
 *    Type page is HSTCutoffPage (FUN_18015e100, mcmillres 3027 - greyed as every Type page here) and its Cut Parameters page
 *    HSTCutoffCutPage (FUN_18015d790, mcmillres 3025 "Cutoff Cut parameters": compensation type / direction, tip comp, roll,
 *    infinite look ahead, stock to leave - the contour page's record fields, no enable function); title "2D Toolpaths - Tab
 *    Cutoff" (FUN_18026d220 case 0x82: mccoreres 4415).
 *  - the Linking Parameters page = mcmillres dialog 25596 (HSTLinkPageBase 0x63fc; DDX FUN_180239350,
 *    formats/ui_fields_mcmill.tsv): Clearance (check 1349, value 1330 = op +0x108, Absolute / Incremental 1331 / 1332 =
 *    +0x110, "Use clearance only at the start and end of operation" 1059 = +0x2b1), Retract (check 25376 = +0x121, value
 *    1389 = +0x118, 1115 / 1116 = +0x120), Feed plane (1392 = +0x128, 1374 / 1375 = +0x130), Top of stock (1550 =
 *    +0x280, 1547 / 1548 = +0x288), Depth (1203 = +0x138, 1126 / 1127 = +0x140). Drill: dialog 25593 (HSTDrillLinkPage
 *    0x63f9) - Clearance, Retract (the feed plane field +0x128 / +0x130, no on / off), Top of stock, Depth.
 *    The "..." buttons (1345 / 1052 / 1053 / 1546 / 1100 -> HSTLinkPageBase::OnClearancePb / OnRetractPb / OnFeedPb /
 *    OnStockPb / OnDepthPb) all call mccore tp_select_depth (0x1800cd910): prompt 5321 "Select point", a get_event /
 *    AutoCursor point pick, the field = the picked point's Z in op coords (wpt_to_op), minus a reference only for
 *    Incremental fields (the chain Z for Depth / Top of stock, the top of stock for the planes). There is NO
 *    top / bottom-of-model snap in the original.
 *    Incremental references (mccore set_depths): top / depth from the chain, the others from the top of stock.
 * Not done (greyed pages): Toolpath Type (changing the type: TreeStyleDlgBase::ChangeOperationType), Holder, Axis Control /
 * Rotary Axis Control. Batch 11 (2026-10-24): Coolant 6546, Canned Text 6548, Home / Ref. Points 6554 and Misc Values 6555 are
 * template pages (pf_dlg; cam_postvals.c; formats/opdlg_port_audit.md 17). Every page with a decoded template is drawn from
 * it ("template pages" at the end of this file, 2026-10-22; Thread Mill's Linking page is its own template 2837, not
 * 25596). The frame (dialog 6544), tree images, toolbar, quick view, image buttons and page pictures: "dialog frame
 * graphics" below (formats/mcamui_dialogs.md 1 "Graphics").
 */
/* FUN_18014a700's string list (FUN_1800c0200 calls): mccoreres (GetCoreString) or mcmillres (CString::LoadString) */
const char *const PG_TITLE[PG_N] = { "", "Toolpath Type", "Tool", "Holder", "Stock", "Cut Parameters", "Depth Cuts",
    "Finish Passes", "Trochoidal Motion", "Transitions", "Lead In/Out", "Break Through", "Linking Parameters",
    "Home / Ref. Points", "Arc Filter / Tolerance", "Planes (WCS)", "Coolant", "Canned Text", "Misc Values", "Axis Control",
    "Axis Combination", "Rotary Axis Control", "Multiple Heads", "Multi Passes", "Tip Comp", "Tabs", "Roughing", "Finishing",
    "Rough/Finish", "Entry Motion", "Tab Cutoff", "Contour Wall", "Finishing", "HST Leads", "Setup"};

/* which pages this op type's dialog has (MakeSpecificPages slot list x the factory's NULL cases) */
int pg_has(const CamParams *p, int s) {
    int t = p->type, chain = t == CAM_CONTOUR || t == CAM_POCKET || t == CAM_FACE || t == CAM_SLOT;
    int hst = t == CAM_DYNAMIC || t == CAM_AREA || t == CAM_DYNCONTOUR || t == CAM_PEEL;
    int style = t == CAM_DYNAMIC ? 0 : t == CAM_AREA ? 1 : t == CAM_DYNCONTOUR ? 2 : 3;   /* +0x656 / +0x32b0 */
    if (t == CAM_SRPOCKET || t == CAM_SRPARALLEL || t == CAM_SRMPOCKET) switch (s) {   /* srough: the classic surface sheet's tabs (CSurfaceSheet FUN_1802b6550,
                                              formats/surf_rough.md 4): Tool parameters, Surface parameters 999, Rough parameters 1310 + Pocket
                                              parameters 242 (op 10) / Rough parallel parameters 1039 (op 5); shown in this dialog's tree (ours) */
    case PG_TOOL: case PG_CUT: case PG_ROUGH: return 1;
    case PG_FINISH: return t == CAM_SRPOCKET || t == CAM_SRMPOCKET;   /* op 131: Pocket parameters 1312 (surf_classic2.md 1.2) */
    default: return 0;
    }
    if (t == CAM_ENGRAVE) switch (s) {   /* Engrave: the property sheet "Engraving" (engraveres 8): Toolpath parameters (mwEngraveToolPage),
                                            Engraving parameters 1802, Roughing/Finishing 1803 (formats/engrave.md 2.1) */
    case PG_TOOL: case PG_CUT: case PG_ROUGH: return 1;
    default: return 0;
    }
    if (t == CAM_SFCONTOUR || t == CAM_SRPROJECT || t == CAM_BLEND || t == CAM_SRPLUNGE || t == CAM_SFRADIAL || t == CAM_SFPROJECT || t == CAM_SFPENCIL) switch (s) {   /* Surface Finish Pencil: Tool, Surface parameters 999, Finish pencil parameters 240 (sheet case 0x27 ms l.450342-450354: CMsPnclPg, tab string 131) */   /* Blend: Tool, Surface parameters 999, Finish blend parameters 1040 (sheet case 0x6d, surf_classic3.md 3.3) */   /* Surface Finish Contour: Tool parameters, Surface parameters 999, Contour parameters 180 (formats/surf_classic2.md 3.2) */
    case PG_TOOL: case PG_CUT: case PG_ROUGH: return 1;
    default: return 0;
    }
    if (t == CAM_SCALLOP) switch (s) {   /* Scallop: Cut Parameters 1328 and the Steep / Shallow page 1342 (in the PG_ROUGH slot, ours) + the common ones */
    case PG_TYPE: case PG_TOOL: case PG_HOLDER: case PG_CUT: case PG_ROUGH: case PG_LINK: case PG_PLANES: case PG_COOL: case PG_CANNED: case PG_MISC:
        return 1;
    default: return 0;
    }
    if (t == CAM_RASTER || t == CAM_AREAROUGH || t == CAM_SPIRAL || t == CAM_RADIAL || t == CAM_HSTPROJECT || t == CAM_PENCIL || t == CAM_HORIZAREA) switch (s) {   /* Pencil: its Cut Parameters page 1329 (formats/pencil.md 4) */   /* Raster / Area Roughing / Spiral / Radial: the decoded Cut Parameters page 1330 / 1326 / 1338 / 1339 and the common ones (ours: the tree) */
    case PG_TYPE: case PG_TOOL: case PG_HOLDER: case PG_CUT: case PG_LINK: case PG_PLANES: case PG_COOL: case PG_CANNED: case PG_MISC:
        return 1;
    default: return 0;
    }
    if (t == CAM_WATERLINE) switch (s) {   /* Waterline: the decoded pages (Cut Parameters 1324, Transitions 1341) and the common ones, as OptiRough (ours: the tree) */
    case PG_TYPE: case PG_TOOL: case PG_HOLDER: case PG_CUT: case PG_TRANS: case PG_LINK: case PG_PLANES: case PG_COOL: case PG_CANNED: case PG_MISC:
        return 1;
    default: return 0;
    }
    if (t == CAM_OPTIROUGH) switch (s) {   /* 3drough: Dynamic OptiRough - the HST page factory of mcmsurf is NOT DECODED; the pages shown are
                                              the decoded ones (Cut Parameters 1465, Transitions 1430) and the common ones (ours: the tree) */
    case PG_TYPE: case PG_TOOL: case PG_HOLDER: case PG_CUT: case PG_TRANS: case PG_LINK: case PG_PLANES: case PG_COOL: case PG_CANNED: case PG_MISC:
        return 1;
    default: return 0;
    }
    switch (s) {
    case PG_TYPE: case PG_TOOL: case PG_HOLDER: case PG_CUT: case PG_LINK: case PG_HOME: case PG_PLANES: case PG_COOL:
    case PG_CANNED: case PG_MISC: case PG_AXCOMBO: case PG_ROTARY: return 1;
    case PG_AXIS: return 1;                                         /* tree node 19 shows page 20 (InsertTreeItem(0x13 .. 0x14)) */
    case PG_ARC: return chain || hst;                               /* not in the point-based slot list */
    case PG_STOCK: return hst && style < 2;
    case PG_DEPTH: return chain || hst || t == CAM_CIRCLE;          /* point based: circle mill only */
    case PG_BREAK: return (chain && t != CAM_FACE) || hst || t == CAM_CIRCLE;
    case PG_LEAD: return t == CAM_CONTOUR || t == CAM_POCKET || t == CAM_CIRCLE || t == CAM_THREAD || (hst && style >= 2);
    case PG_MULTI: return t == CAM_CONTOUR || t == CAM_THREAD;
    case PG_TABS: case PG_TABCUT: return t == CAM_CONTOUR && !p->ct_cutoff_op;   /* opcode 0x82: MakeSpecificPages FUN_18026d480
                                                                    l.424900 skips slots 0x19 / 0x1e, factory FUN_18026c800 l.424622 / 424654 NULL */
    case PG_ROUGH: case PG_FINISH: return t == CAM_POCKET || t == CAM_CIRCLE;
    case PG_ENTRY: return t == CAM_POCKET || (hst && style == 0);
    case PG_ROUGHFIN: return t == CAM_SLOT || t == CAM_HELIX;
    case PG_TIP: return t == CAM_DRILL;
    case PG_TROCH: case PG_TRANS: case PG_HSTLEAD: return hst && style == 1;
    case PG_FINPASS: return hst && style == 3;
    case PG_WALL: case PG_FINISH2: return hst && style == 2;
    }
    return 0;
}
/* pages this program fills; the others are listed greyed. Every page with a decoded template is shown (pf_dlg, the
   "template pages" below: live where this program's generators take the value, greyed otherwise); the 2D chamfer
   contour keeps Depth Cuts and Tabs greyed as before (its generator has no depth / tab steps) */
int pg_done(const CamParams *p, int s) {
    if ((s == PG_DEPTH || s == PG_TABS) && p->type == CAM_CONTOUR && p->contour_type == 1) return 0;
    if (pf_dlg(p, s)) return 1;
    switch (s) {
    case PG_TOOL: case PG_CUT: case PG_LINK: case PG_COOL: return 1;
    case PG_PLANES: return 1;                                       /* PlanesPageBase (dialog 6547): core_ui cui_planes.c */
    }
    return 0;
}
/* tree order and nesting of FUN_18014a700: parent slot of each slot (0 = root), in insertion order */
static const struct { int s, parent; } PG_TREE[] = {
    { PG_TYPE, 0 }, { PG_TOOL, 0 }, { PG_HOLDER, 0 }, { PG_STOCK, 0 }, { PG_CUT, 0 },
    { PG_ROUGH, PG_CUT }, { PG_ENTRY, PG_ROUGH }, { PG_ROUGHFIN, PG_CUT }, { PG_FINISH, PG_CUT }, { PG_LEAD, PG_FINISH },
    { PG_WALL, PG_CUT }, { PG_DEPTH, PG_CUT }, { PG_ENTRY, PG_CUT }, { PG_FINISH2, PG_CUT }, { PG_LEAD, PG_FINISH2 },
    { PG_FINPASS, PG_CUT }, { PG_LEAD, PG_FINPASS }, { PG_TROCH, PG_CUT }, { PG_TRANS, PG_CUT }, { PG_LEAD, PG_CUT },
    { PG_BREAK, PG_CUT }, { PG_MULTI, PG_CUT }, { PG_TABS, PG_CUT }, { PG_TABCUT, PG_TABS },
    { PG_LINK, 0 }, { PG_TIP, PG_LINK }, { PG_HSTLEAD, PG_LINK }, { PG_HOME, PG_LINK },
    { PG_ARC, 0 }, { PG_PLANES, 0 }, { PG_COOL, 0 }, { PG_CANNED, 0 }, { PG_MISC, 0 }, { PG_AXIS, 0 }, { PG_ROTARY, PG_AXIS },
};
/* a slot appears once, under the first parent present (the builder's if / else chains) */
int pg_parent_ok(const CamParams *p, int i) {
    int s = PG_TREE[i].s, par = PG_TREE[i].parent;
    if (!pg_has(p, s) || (par && !pg_has(p, par))) return 0;
    for (int k = 0; k < i; k++) if (PG_TREE[k].s == s && pg_has(p, PG_TREE[k].s) && (!PG_TREE[k].parent || pg_has(p, PG_TREE[k].parent))) return 0;
    if (s == PG_ENTRY && par == PG_CUT && pg_has(p, PG_ROUGH)) return 0;
    if (s == PG_LEAD && par == PG_CUT && (pg_has(p, PG_FINISH) || pg_has(p, PG_FINPASS) || pg_has(p, PG_FINISH2))) return 0;
    return 1;
}

/* heights: Mastercam's per-op flags (op offsets +0x110 / +0x111 / +0x2b1 / +0x120 / +0x121 / +0x130 / +0x288 / +0x140) */
OpHFlags pp_hf;                                   /* the dialog's op */
int pp_page = PG_CUT;
void hf_from_record(OpHFlags *h, const uint8_t *r, uint32_t n, double cz) {
    #define RB(o) ((o) < n ? r[o] : 0)
    h->clr_inc = RB(0x178); h->clr_on = RB(0x179); h->clr_end = RB(0x319); h->ret_inc = RB(0x188); h->ret_on = RB(0x189);
    h->fp_inc = RB(0x198); h->top_inc = RB(0x2f0); h->dep_inc = RB(0x1a8); h->cz = cz;
    double fs = 0; if (0x888 + 8 <= n) memcpy(&fs, r + 0x888, 8); h->fstock = fs;
    {   int oc = n > 0x6d ? (int16_t)(r[0x6c] | r[0x6d] << 8) : 0, bo = cam_bt_offset(oc); double a = 0;   /* record +0x6c = the opcode */
        if (bo && (uint32_t)bo + 16 <= n) memcpy(&a, r + bo + 8, 8);
        h->bt = bo && (uint32_t)bo + 16 <= n ? cam_bt_eff(oc, r[bo], a, fs) : 0; }
    double cv = 0, tv = 0; if (0x2e8 + 8 <= n) { memcpy(&cv, r + 0x170, 8); memcpy(&tv, r + 0x2e8, 8); }
    if (tv == -1e38) tv = 0;
    h->clr_abs = cv + (h->clr_inc ? tv + (h->top_inc ? cz : 0) : 0);
    /* Thread Mill keeps its heights in its own fields (HSTThreadmillLinkPage 2837: clearance 0x1230, absolute; cam_fileop.c
       params_from_record), not the Linking flags 0x170 / 0x179: the stored op's clearance is that field, as
       cam_op_from_file takes it (an unedited OK must hand cam_generate what fileop does) */
    h->thr_clr_on = 0; h->thr_clr = 0;
    if (n >= 0x1230 + 8 && (int16_t)(r[0x6c] | r[0x6d] << 8) == 100) { memcpy(&h->thr_clr, r + 0x1230, 8); h->thr_clr_on = 1; }   /* record +0x6c = the opcode (mcam_load.c), 100 = thread mill */
    #undef RB
}
/* the flags of an op made here: the height record bytes written with it (rpatch, below), else this program's save
   convention (clearance / retract absolute, feed plane incremental except drill: cam_common_apply) */
void hf_from_op(OpHFlags *h, const CamOp *op) {
    const CamParams *p = &op->p; int drill = p->type == CAM_DRILL;
    memset(h, 0, sizeof *h);
    h->clr_on = 1; h->ret_on = p->retract_z > -1e29; h->fp_inc = !drill;
    #define PB(o, var) for (int k = 0; k < p->nrpatch; k++) if (p->rpatch[k].off == (o) && p->rpatch[k].n == 1) var = p->rpatch[k].v[0]
    PB(0x178, h->clr_inc); PB(0x179, h->clr_on); PB(0x319, h->clr_end); PB(0x188, h->ret_inc); PB(0x189, h->ret_on);
    PB(0x198, h->fp_inc); PB(0x2f0, h->top_inc); PB(0x1a8, h->dep_inc);
    #undef PB
    if (op->nchains && op->chains[0].n) { const Ent *e0 = &F.ents[op->chains[0].ent[0]]; h->cz = e0->type == E_ARC ? e0->c.z : e0->a.z; }
    h->clr_abs = p->clearance;
    for (int k = 0; k < p->nrpatch; k++) if (p->rpatch[k].off == 0x170 && p->rpatch[k].n == 8) { memcpy(&h->clr_abs, p->rpatch[k].v, 8); if (h->clr_inc) h->clr_abs += p->top_z; }
    /* the floor stock (record 0x888): the Cut page's edit when it was edited here (pf_apply), else the record the op is saved
       from (the defaults op, mcam_saveops.c) - the value its depth was evaluated with (heights_from_default_op adds it to
       the bottom), so hf_apply's depth 0x1a0 = bottom - floor stock agrees with the record's 0x888. Facing: 0
       (heights_from_default_op: opcode 0x66 takes the constant, not 0x888) */
    {   uint32_t dn = 0; const uint8_t *dr = cam_default_op_record(p->type, mcam_is_metric(&F), &dn);
        if (dr && dn >= 0x888 + 8 && p->type != CAM_FACE) { memcpy(&h->fstock, dr + 0x888, 8); if (!isfinite(h->fstock)) h->fstock = 0; } }
    for (int k = 0; k < p->nrpatch; k++) if (p->rpatch[k].off == 0x888 && p->rpatch[k].n == 8) memcpy(&h->fstock, p->rpatch[k].v, 8);
    h->bt = p->bt_eff;                                     /* the break through its bottom holds (cui_ops.c / cam_params_from_patches) */
}
void rp_put(CamParams *p, int off, const void *v, int n) {      /* as cam_fileop.c rpatch_set, height entries */
    int k = 0; while (k < p->nrpatch && p->rpatch[k].off != off) k++;
    if (k == p->nrpatch) { if (p->nrpatch >= CAM_NRPATCH) { cam_rpatch_full(off); return; } p->nrpatch++; }
    p->rpatch[k].off = (uint16_t)off; p->rpatch[k].n = (uint8_t)n; p->rpatch[k].height = 1; memcpy(p->rpatch[k].v, v, n);
}
/* at OK: the dialog's height fields and flags written over the saved record (values in the record's own convention),
   and the motion's clearance / retract from the on switches (params_from_record's rule) */
void hf_apply(CamParams *p, const OpHFlags *h) {
    int drill = p->type == CAM_DRILL; double top = p->top_z;
    double fp_abs = drill ? p->r_plane : top + p->feed_plane;
    double v; uint8_t b;
    v = h->clr_inc ? h->clr_abs - top : h->clr_abs; rp_put(p, 0x170, &v, 8);
    b = (uint8_t)h->clr_inc; rp_put(p, 0x178, &b, 1); b = (uint8_t)h->clr_on; rp_put(p, 0x179, &b, 1); b = (uint8_t)h->clr_end; rp_put(p, 0x319, &b, 1);
    if (!drill) { double ra = p->retract_z > -1e29 ? p->retract_z : fp_abs; v = h->ret_inc ? ra - top : ra; rp_put(p, 0x180, &v, 8);
                  b = (uint8_t)h->ret_inc; rp_put(p, 0x188, &b, 1); b = (uint8_t)h->ret_on; rp_put(p, 0x189, &b, 1); }
    v = h->fp_inc ? fp_abs - top : fp_abs; rp_put(p, 0x190, &v, 8); b = (uint8_t)h->fp_inc; rp_put(p, 0x198, &b, 1);
    v = h->top_inc ? top - h->cz : top; rp_put(p, 0x2e8, &v, 8); b = (uint8_t)h->top_inc; rp_put(p, 0x2f0, &b, 1);
    double dep = p->final_z - h->fstock + h->bt; v = h->dep_inc ? dep - h->cz : dep; rp_put(p, 0x1a0, &v, 8); b = (uint8_t)h->dep_inc; rp_put(p, 0x1a8, &b, 1);
    p->mc_clr_on = h->clr_on; p->mc_clr_end = h->clr_end; p->mc_ret_on = h->ret_on;
    p->dep_inc = h->dep_inc != 0; p->top_inc = h->top_inc != 0; p->dep_cz = h->cz;
    p->ht_inc = (h->clr_inc ? 1 : 0) | (h->ret_inc ? 2 : 0) | (h->fp_inc ? 4 : 0);   /* batch 7: Facing's adjust_depths (cam2d_face.c) */
    if (!drill && !h->ret_on) p->retract_z = -1e30;
    p->clearance = h->clr_on ? h->clr_abs : h->ret_on && p->retract_z > -1e29 ? p->retract_z : fp_abs;
    if (p->type == CAM_THREAD && h->thr_clr_on) p->clearance = h->thr_clr;   /* Thread Mill: its own clearance field (hf_from_record) */
}

/* one Linking row: [check] Button... value (Absolute / Incremental); the value is shown in the chosen convention */
/* the "..." buttons' pick targets (HSTLinkPageBase::OnClearancePb 1345 / OnRetractPb 1052 / OnFeedPb 1053 /
   OnStockPb 1546 / OnDepthPb 1100, all calling mccore tp_select_depth) */
/* tp_select_depth (mccore 0x1800cd910): the field gets the picked point's Z in op coordinates (wpt_to_op; the
   C-plane here is always Top with origin 0, so the world Z). Mastercam stores the Z minus a reference (chain Z for
   Depth / Top of stock, top of stock for the planes) when the field is Incremental; this dialog stores absolutes and
   shows incremental relative, so writing the absolute gives the same displayed value. No top / bottom of model snap
   exists in the original (the observation = AutoCursor snapping to entity points, which sit at the model's Zs). */
int lk_pick;                                     /* a Linking page "..." button waits for a graphics point */
void lk_pick_z(double z) {
    CamParams *p = &pending.p; OpHFlags *h = &pp_hf;
    switch (lk_pick) {
    case LKP_CLR: h->clr_abs = z; break;
    case LKP_RET: if (p->type == CAM_DRILL) p->r_plane = z; else p->retract_z = z; break;
    case LKP_FP: p->feed_plane = z - p->top_z; break;                                   /* the field stores top-relative */
    case LKP_TOP: if (p->type != CAM_DRILL) p->feed_plane += p->top_z - z; p->top_z = z; break;   /* the feed plane keeps its Z */
    case LKP_DEPTH: p->final_z = z; break;
    }
}

/* ---- dialog frame graphics and page pictures (formats/mcamui_dialogs.md 1 "Graphics") ----
   Frame = mccoreres dialog 6544 (567 x 395 du; ToolpathDlg::ToolpathDlg(CWnd*, int) 0x18088f1a0 = MCDialog(0x1990), the
   2D dialogs' TreeStyleDlgBase ctor 0x1809097f0 uses it): toolbar (ToolpathDlg::InitToolBar 0x180891be0, 24 x 24 images in
   34 x 33 buttons, docked top), tree 25455 (4,33 115x170), "Quick View Settings" 9000 (4,211), list 3329 (4,223 115x129),
   legend 3608 / 3610 "= edited" and 3611 / 3620 "= disabled", page frame 3667 (127,33 435x319), OK 1 / Cancel 2 / Apply
   12321 / Help 9 at 404 / 444 / 484 / 526, 373 (36x18).
   Images come from the user's icon pack (tools/extract_icons.py): "<dll>_BMP_<id>.png", "<dll>_BMPK_<id>.png" (192,192,192
   clear, the bitmaps Mastercam draws transparent) and "mcmillres_PNG_<id>.png"; absent ones leave a blank frame. */
const uint8_t *pp_rec; uint32_t pp_rec_n;   /* the record the dialog was seeded from (stored op / defaults op), NULL = none */
int pp_tstate[PG_N], pp_tree_fresh = 1, pp_mp_pic;   /* tree item images; Multi Passes picture after a toggle */
#define PP_RB(o) (pp_rec && (uint32_t)(o) < pp_rec_n ? pp_rec[o] : 0)
int pp_rs16(uint32_t o) { int16_t v = 0; if (pp_rec && o + 2 <= pp_rec_n) memcpy(&v, pp_rec + o, 2); return v; }
int pp_page_pic(const CamParams *p, int pg, int *x, int *y, int *w, int *h, char *file, size_t fn) {
    int t = p->type, id = 0, png = 0, k = 0;
    #define AT(X_, Y_, W_, H_) (*x = (X_), *y = (Y_), *w = (W_), *h = (H_))
    switch (pg) {
    case PG_LINK:     /* static 624: HSTLinkPageBase::InitPage 0x180174680 SetBitmap 0x648d; HSTDrillLinkPage::InitPage
                         0x180163a40 0x64b6 (SetTransparent, 0xc0c0c0) */
        AT(6, 7, 122, 279); if (t == CAM_DRILL) { id = 25782; k = 1; } else id = 25741; break;
    case PG_DEPTH:    /* static 2918 (dialog 25598; pocket HSTPocketDepthCutsPage 3030): HSTDepthCutsPage::MapToDlg 0x1801613f0
                         -> DepthCutsPageBase::SetControlBitmap 0x180063860 (flag 0 / 4 = Tapered walls 1381 off / on: contour op
                         +0x12a2, pocket +0x1220, record 0x130a / 0x1288) = 0x6468 + flag; transparent 0xc0c0c0 (InitPage) */
        if (t == CAM_POCKET) AT(181, 29, 150, 72); else AT(203, 53, 150, 72);
        if (t == CAM_CIRCLE || t == CAM_FACE || t == CAM_SLOT) id = 25704;   /* opcodes 0x12 / 0x66 / 0x69: the taper check is hidden */
        else id = 25704 + ((t == CAM_CONTOUR ? p->dc_taper : PP_RB(t == CAM_POCKET ? 0x1288 : 0x130a)) ? 4 : 0);   /* contour: the live check (batch 3) */
        k = 1; break;
    case PG_LEAD:     /* static 2917 UIPNGImage (HSTLeadIOPageBase, dialogs 3013 / 2793): FUN_180172910 - entry line radio 25167 /
                         1160 (+0x4998 = op +0x891 == 0, record 0x8f9) x exit 25182 / 25188 (+0x3b98 = op +0x8d9 == 0, record
                         0x941): PNG 0x6439 / 0x643b / 0x6440 / 0x643a */
        if (t != CAM_CONTOUR && t != CAM_POCKET) return 0;
        if (t == CAM_POCKET) AT(175, 36, 80, 125); else AT(176, 28, 80, 117);
        /* the radios' states: CamParams since batch 4 (cam_fileop.c LEADS reads 0x8f9 / 0x941; ops made here: the defaults op's) */
        { int e = !p->lead_in_perp, xx = !p->lead_out_perp; id = !e ? (!xx ? 25657 : 25659) : (!xx ? 25664 : 25658); png = 1; } break;
    case PG_MULTI:    /* static 2939 (25591): HSTMultiPassPage::InitPage 0x180176fe0 0x6535 (transparent 0xc0c0c0);
                         OnBnClickedMultipassCb 0x180177a00: 0x6535 off / 0x6536 on */
        if (t != CAM_CONTOUR) return 0;
        AT(213, 38, 169, 139); id = pp_mp_pic ? 25910 : 25909; k = 1; break;
    case PG_TABS:     /* static 1076 (2834): FUN_18018f050 - Full / Partial 1067 / 1206 (+0x3668 = op +0x1330 == 0, record
                         0x1398) x Vertical / Ramp moves 1208 / 1201 (+0x7e18 = op +0x1334, record 0x139c) */
        AT(214, 21, 188, 216);
        { int part = pp_rec ? !PP_RB(0x1398) : 0, ramp = pp_rs16(0x139c) != 0;
          id = !part ? (ramp ? 25769 : 25764) : (ramp ? 25775 : 25758); } k = 1; break;
    case PG_ROUGH:    /* static 617 (3003): FUN_1801843a0 - 0x64ee + (radio 2170 group +0x6e60 = op +0x13a0, record 0x1408, != 0) */
        if (t != CAM_POCKET) return 0;
        AT(117, 183, 115, 47); id = 25838 + (pp_rs16(0x1408) != 0); k = 1; break;
    case PG_FINISH:   /* static 2186 (3004): FUN at mcmill_all.c l.266454 SetBitmap 0x64d9 (transparent 0xc0c0c0) */
        if (t != CAM_POCKET) return 0;
        AT(19, 135, 158, 132); id = 25817; k = 1; break;
    case PG_ROUGHFIN: /* slot 3020 static 3093: FUN_18018dda0 0x64b8; helix bore 2823 static 3094: FUN_18016e480 0x6517 */
        if (t == CAM_SLOT) { AT(222, 18, 198, 219); id = 25784; } else if (t == CAM_HELIX) { AT(216, 16, 205, 218); id = 25879; } else return 0;
        break;
    case PG_TIP:      /* static 1346 (25594): HSTDrillTipCompPage::InitPage 0x180165440 SetBitmap 0x64d3 */
        AT(25, 28, 127, 136); id = 25811; break;
    case PG_CUT:
        if (t == CAM_DRILL) {   /* static 2938 (25595): DrillCyclePageBase::UpdateCycleBitmap 0x18028c780 by the cycle combo */
            static const int cy[8] = { 25804, 25807, 25802, 25810, 25808, 25809, 25805, 25806 };
            AT(252, 11, 177, 148); id = p->drill_cycle >= 0 && p->drill_cycle < 8 ? cy[p->drill_cycle] : 25803; break;
        }
        if (t == CAM_SLOT) { AT(243, 100, 168, 174); id = 25783; break; }   /* static 3132 (2944): FUN_18018d3b0 SetBitmap 0x64b7 */
        return 0;
    default: return 0;
    }
    #undef AT
    snprintf(file, fn, png ? "mcmillres_PNG_%d.png" : k ? "mcmillres_BMPK_%d.png" : "mcmillres_BMP_%d.png", id);
    return 1;
}
/* HST2DCutParametersPageBase::SetStockToLeaveBmp 0x180147720 (static 2908): walls / floors stock against tol */
int pp_stock_bmp(double w, double f) {
    double tl = cfg_sys_tol;
    if (w - tl > 0 && f - tl > 0) return 25746;
    if (w - tl > 0 && fabs(f) < tl) return 25747;
    if (fabs(w) < tl && f - tl > 0) return 25744;
    if (tl + w < 0) { if (tl + f < 0) return 25948; if (fabs(f) < tl) return 25949; }
    if (fabs(w) >= tl || tl + f >= 0) return 25745;
    return 25947;
}
/* IsPageDisabled overrides (vtable +0x3e8; the base ToolpathPageBase 0x180898fa0 returns 0): ToolpathDlg::
   InitializeTreeStates 0x180892490 posts state 2 for these at open */
int pp_disabled(const CamParams *p, int s) {
    switch (s) {
    case PG_DEPTH: return !(p->mc_depth_on || p->stepdown > 0);  /* DepthCutsPageBase 0x1800634c0: op +0x800 (record 0x868) off
                                                                    (the engines' rule: or a max rough step, cam2d.c hst_depths) */
    case PG_LEAD: return p->type == CAM_CONTOUR && !p->lead_on;  /* HSTContourLeadIOPage FUN_180171300: +0x888 (record 0x8f0) */
    case PG_MULTI: return p->type == CAM_CONTOUR && p->finish_n <= 0 && !(p->ct_mp_rec && p->mc_mp_on);   /* HSTMultiPassPage 0x1801772b0: +0x840 (record
                                                                    0x8a8; finish passes here also mean on, mcam_saveops.c) */
    case PG_TABS: return p->tabs <= 0;                           /* HSTTabPage FUN_18018f030: +0x1318 (record 0x1380) */
    case PG_TABCUT: return p->type == CAM_CONTOUR && p->tab_cut == 0;   /* HSTTabSettingsPage FUN_180191550: +0x138a (record 0x13f2) == 0 */
    case PG_FINISH: return p->type == CAM_POCKET && p->finish_n <= 0;   /* HSTPocketFinishPage FUN_18017ed20: +0x11d1 */
    case PG_TIP: return !p->tip_comp;                            /* HSTDrillTipCompPage 0x1801655a0: +0x1200 */
    }
    return 0;
}
/* the toolbar (InitToolBar + ToolpathDlg::AddDefaultsButtons 0x18088faa0, this[0x729c] = 0 images; tooltips mccoreres
   32932 / 32933 / 32935 / 32938 / 32939 / 403). Only Tool Manager... is live (RunToolManager -> this program's Tool Manager). */
void pp_hf_record(const uint8_t *r, uint32_t n, double cz) { hf_from_record(&pp_hf, r, n, cz); pp_rec = r; pp_rec_n = n; pp_tree_fresh = 1; pf_reset(); }
void pp_hf_op(const CamOp *op) { hf_from_op(&pp_hf, op); pp_rec = NULL; pp_rec_n = 0; pp_tree_fresh = 1; pf_reset(); }
void pp_hf_apply(void) { hf_apply(&pending.p, &pp_hf); }

/* ---- Linking Parameters rows (mcmillres dialog 25596 / drill 25593; was opparams_ui.inc lk_row / page_linking).
   One row: [check] Button... value (Absolute / Incremental); the value is shown in the chosen convention (incremental:
   relative to the top of stock for Clearance / Retract / Feed plane, to the chain Z for Top of stock / Depth, mccore
   set_depths). The front end draws the rows and reports a change with lk_set; the pick buttons arm lk_pick. */
static int lk_drill(void) { return pending.p.type == CAM_DRILL; }
/* the page's per-frame writes (page_linking): the Retract value shown when it is off is top + feed plane and is written
   back, the feed plane goes through its absolute value (top + value - top) */
void lk_frame(void) {
    CamParams *p = &pending.p; double top = p->top_z;
    if (lk_drill()) return;
    double ret = p->retract_z > -1e29 ? p->retract_z : top + p->feed_plane;
    p->retract_z = ret;
    double fp = top + p->feed_plane;
    p->feed_plane = fp - top;
}
int lk_rows(LkRow *r) {
    CamParams *p = &pending.p; OpHFlags *h = &pp_hf; int drill = lk_drill(), n = 0; double top = p->top_z;
    #define ROW(ID_, BTN_, HAS_, ON_, ABS_, INC_, REF_, PK_) do { LkRow *x_ = &r[n++]; x_->id = ID_; x_->btn = BTN_; x_->has_on = HAS_; \
        x_->on = ON_; x_->inc = INC_; x_->pick = PK_; x_->live = !(HAS_) || (ON_); x_->shown = (INC_) ? (ABS_) - (REF_) : (ABS_); } while (0)
    ROW(LK_CLR, "Clearance...", 1, h->clr_on, h->clr_abs, h->clr_inc, top, LKP_CLR);
    if (drill) ROW(LK_RET, "Retract...", 0, 1, p->r_plane, h->fp_inc, top, LKP_RET);   /* 25593: the feed plane field, no switch */
    else {
        double ret = p->retract_z > -1e29 ? p->retract_z : top + p->feed_plane;
        ROW(LK_RET, "Retract...", 1, h->ret_on, ret, h->ret_inc, top, LKP_RET);
        ROW(LK_FP, "Feed plane...", 0, 1, top + p->feed_plane, h->fp_inc, top, LKP_FP);
    }
    ROW(LK_TOP, "Top of stock...", 0, 1, p->top_z, h->top_inc, h->cz, LKP_TOP);
    ROW(LK_DEP, "Depth...", 0, 1, p->final_z, h->dep_inc, h->cz, LKP_DEPTH);
    #undef ROW
    return n;
}
void lk_set(int row, int what, double v) {
    CamParams *p = &pending.p; OpHFlags *h = &pp_hf; int drill = lk_drill(); double top = p->top_z;
    LkRow r[5]; int n = lk_rows(r), k = -1;
    for (int i = 0; i < n; i++) if (r[i].id == row) k = i;
    if (k < 0) return;
    if (what == LK_ON) {
        if (row == LK_CLR) h->clr_on = v != 0; else if (row == LK_RET && !drill) h->ret_on = v != 0;
        return;
    }
    if (!r[k].live) return;                               /* the value, buttons and radios follow the row's switch */
    if (what == LK_PICK) { lk_pick = r[k].pick; snprintf(cam_msg, sizeof cam_msg, "Select point"); return; }   /* mccoreres 5321 */
    int *inc = row == LK_CLR ? &h->clr_inc : row == LK_RET ? (drill ? &h->fp_inc : &h->ret_inc) : row == LK_FP ? &h->fp_inc
             : row == LK_TOP ? &h->top_inc : &h->dep_inc;
    if (what == LK_INC) { *inc = v != 0; return; }
    double ref = row == LK_TOP || row == LK_DEP ? h->cz : top, a = *inc ? v + ref : v;
    switch (row) {
    case LK_CLR: h->clr_abs = a; break;
    case LK_RET: if (drill) p->r_plane = a; else p->retract_z = a; break;
    case LK_FP: p->feed_plane = a - top; break;                                  /* the field stores top-relative */
    case LK_TOP: { double t0 = p->top_z, fpa = top + p->feed_plane; p->top_z = a;
        if (p->top_z != t0 && !drill) p->feed_plane = fpa - p->top_z; } break;  /* the feed plane keeps its Z (absolute here) */
    case LK_DEP: p->final_z = a; break;
    }
}
void lk_set_clr_end(int on) { if (pp_hf.clr_on) pp_hf.clr_end = on != 0; }   /* "Use clearance only at the start and end of operation" */
/* a graphics click while lk_pick is armed (mcamui handle_click): the AutoCursor point under the click (an entity snap
   carries its Z; a miss = the construction-plane depth gz) */
void lk_click(double wx, double wy, double gz) {
    V3 p = { wx, wy, gz };
    geo_snap(&p, NULL, -1, 0); lk_pick_z(p.z); lk_pick = 0; cam_msg[0] = 0;
}

/* Contour Tabs page Position... 1098 (HSTTabPage FUN_180190070, decomp/mcmill_all.c l.277719-278000, + mccore position_tab
   @180226b10, decomp/mccore_tabs.c l.617-955). The dialog is suspended (AfxSuspendWindow) with the tabs shown (displayTabs).
   tb_pick 1: get_event(mcmillres 2837 "Select chain to add tabs to (press <Enter> when done)", entity mask): the entity must be
   one of the op's chains (get_ent_assoc kind 2 with the op's id) - another entity ends the command; Enter ends it.
   tb_pick 2: position_tab(chain, new tab) with prompt 2838 "Place tab at desired position": on every mouse move the tab goes
   to the chain entity nearest the cursor closer than sel_dist (dist_to_finite_line / dist_to_arc / dist_to_spline; t_on_line,
   the arc's t), pt_on_ent_from_t; a click keeps it (rec +0x30 t, +4 the entity, +0x10 the point) and the chain is saved with
   the tab list at once (save_chain); Esc / Enter in position_tab drop this tab, back to tb_pick 1.
   get_event 1 at the chain prompt (Esc / right-click: analyze.md, extrude.md 1.3 [inferred for this call]) takes the last tab
   placed in this run out of the list, saves the chain and ends.
   Ours: a tab is a centre point (CamParams tab_xy, what this program cuts - cam2d.c cut_path); the click places the tab at the
   nearest chain point to the click itself (Mastercam keeps the last mouse-move position: the same point once the cursor has
   moved there); splines are not snapped; the tabs are in the pending op (Mastercam's save_chain writes the chain at once, so
   its tabs outlive the dialog's Cancel - here Cancel drops them). */
int tb_pick, tb_hok; double tb_hx, tb_hy;
static int tb_chain = -1, tb_added;
static int tb_near(int ci, double x, double y, double lim, double *px, double *py) {   /* nearest point on chain ci within lim */
    if (ci < 0 || ci >= pending.nchains) return 0;
    const Chain *c = &pending.chains[ci]; double best = lim; int got = 0;
    for (int k = 0; k < c->n; k++) {
        int e = c->ent[k]; if (e < 0 || e >= F.nents) continue;
        const Ent *en = &F.ents[e]; double qx, qy;
        if (en->type == E_LINE) {
            double dx = en->b.x - en->a.x, dy = en->b.y - en->a.y, L2 = dx * dx + dy * dy, t = L2 > 0 ? ((x - en->a.x) * dx + (y - en->a.y) * dy) / L2 : 0;
            if (t < 0) t = 0; if (t > 1) t = 1;
            qx = en->a.x + dx * t; qy = en->a.y + dy * t;
        } else if (en->type == E_ARC) {
            double a = atan2(y - en->c.y, x - en->c.x), u = en->sw != 0 ? remainder(a - en->a0 - en->sw / 2, 2 * M_PI) / en->sw + 0.5 : 0;
            if (u < 0) u = 0; if (u > 1) u = 1;
            qx = en->c.x + en->r * cos(en->a0 + en->sw * u); qy = en->c.y + en->r * sin(en->a0 + en->sw * u);
        } else continue;
        double d = hypot(qx - x, qy - y);
        if (d < best) { best = d; *px = qx; *py = qy; got = 1; }
    }
    return got;
}
static double tb_seldist(void) { return SEL_APERTURE_PX * scene_view_height() / (vp_h > 0 ? vp_h : 1); }   /* sel_dist */
static void tb_end(void) { tb_pick = 0; tb_hok = 0; tb_chain = -1; cam_msg[0] = 0; overlay_dirty = 1; }
void tb_start(void) {
    if (!show_params || pending.p.type != CAM_CONTOUR) return;
    tb_pick = 1; tb_hok = 0; tb_chain = -1; tb_added = 0; overlay_dirty = 1;
    snprintf(cam_msg, sizeof cam_msg, "Select chain to add tabs to (press <Enter> when done)");   /* mcmillres 2837 */
}
void tb_click_ent(int e) {   /* tb_pick 1: the picked entity (-1: none) */
    if (tb_pick != 1 || e < 0) return;
    for (int ci = 0; ci < pending.nchains; ci++) for (int k = 0; k < pending.chains[ci].n; k++)
        if (pending.chains[ci].ent[k] == e) {
            tb_chain = ci; tb_pick = 2; tb_hok = 0; overlay_dirty = 1;
            snprintf(cam_msg, sizeof cam_msg, "Place tab at desired position");   /* mcmillres 2838 */
            return;
        }
    tb_end();                                          /* not the op's chain: get_ent_assoc fails -> the command ends */
}
void tb_move(double x, double y) {
    if (tb_pick != 2) return;
    double px, py;
    if (tb_near(tb_chain, x, y, tb_seldist(), &px, &py)) { tb_hx = px; tb_hy = py; tb_hok = 1; overlay_dirty = 1; }
}
void tb_click_pt(double x, double y) {   /* tb_pick 2: the click keeps the tab */
    if (tb_pick != 2) return;
    tb_move(x, y);
    if (!tb_hok) return;                               /* no chain entity near yet: position_tab keeps waiting */
    CamParams *p = &pending.p, before = pending.p; OpHFlags hb = pp_hf;
    if (p->ntab_xy < CAM_TAB_MAX) {
        p->tab_ed[p->ntab_xy] = 0;   /* a new tab: the op's values (cam2d.h tab_ow) */
        p->tab_xy[2 * p->ntab_xy] = tb_hx; p->tab_xy[2 * p->ntab_xy + 1] = tb_hy; p->ntab_xy++; tb_added++;
        if (p->tabs > 0) p->tabs = p->ntab_xy;
        pp_note_change(&before, &hb);
    }
    tb_pick = 1; tb_hok = 0; tb_chain = -1; overlay_dirty = 1;
    snprintf(cam_msg, sizeof cam_msg, "Select chain to add tabs to (press <Enter> when done)");
}
void tb_key(int enter) {   /* Enter / Esc */
    if (!tb_pick) return;
    if (tb_pick == 2) { tb_pick = 1; tb_hok = 0; tb_chain = -1; overlay_dirty = 1;   /* position_tab: this tab dropped */
        snprintf(cam_msg, sizeof cam_msg, "Select chain to add tabs to (press <Enter> when done)"); return; }
    if (!enter && tb_added > 0) {                      /* get_event 1: the last tab of this run out */
        CamParams *p = &pending.p, before = pending.p; OpHFlags hb = pp_hf;
        p->ntab_xy--; if (p->tabs > 0) p->tabs = p->ntab_xy > 0 ? p->ntab_xy : 1; pp_note_change(&before, &hb);
    }
    tb_end();
}
/* draw_tabs (mccore @180220950) while Position... runs: the chain (draw_sel_chain), a marker per tab (FUN_18021b530); ours: the
   fast-marker square for the markers, the tab centre only (draw_tabs also marks its two ends, FUN_1802256c0) */
void tb_overlay(float **pv, int *pn, int *pcap) {
    if (!tb_pick) return;
    float rgb[3]; mc_rgb(cfg_select_color, rgb);
    if (tb_chain >= 0 && tb_chain < pending.nchains)
        for (int k = 0; k < pending.chains[tb_chain].n; k++) ov_entity(pv, pn, pcap, pending.chains[tb_chain].ent[k], rgb);
    double z = pending.p.top_z;
    for (int t = 0; t < pending.p.ntab_xy; t++) ov_fast_marker(pv, pn, pcap, (V3){ pending.p.tab_xy[2 * t], pending.p.tab_xy[2 * t + 1], z });
    if (tb_pick == 2 && tb_hok) ov_fast_marker(pv, pn, pcap, (V3){ tb_hx, tb_hy, z });
}

/* ---- the frame (mccoreres dialog 6544, 567 x 395 du; "dialog frame graphics" above) */
const PpRect PP_FRAME[PPF_N] = {
    { 4, 33, 115, 170 },    /* PPF_TREE 25455 */
    { 4, 211, 84, 9 },      /* PPF_QVLABEL 9000 "Quick View Settings" */
    { 4, 223, 115, 129 },   /* PPF_QV 3329 */
    { 4, 361, 10, 10 },     /* PPF_LEG1 3608 (0x5e5) */
    { 4, 374, 11, 10 },     /* PPF_LEG2 3611 (0x5da) */
    { 19, 361, 55, 10 },    /* PPF_LEG1T 3610 "= edited" */
    { 19, 375, 55, 10 },    /* PPF_LEG2T 3620 "= disabled" */
    { 127, 33, 435, 319 },  /* PPF_PAGE 3667 (pages 295 high) */
    { 127, 373, 270, 18 },  /* PPF_STATUS: this program's status line (cam_msg) */
    { 404, 373, 36, 18 },   /* PPF_OK 1 */
    { 444, 373, 36, 18 },   /* PPF_CANCEL 2 */
    { 484, 373, 36, 18 },   /* PPF_APPLY 12321 */
    { 526, 373, 36, 18 },   /* PPF_HELP 9 */
};
/* OK / Cancel / Apply / Help: UIDialog::SetBitmapButtons 0x18000ac10 (uictrls.dll) with the default table of
   AddDefaultBitmapButtons 0x1800094f0 {7003 IDOK, 7005 IDCANCEL, 7004 12321, 7006 IDHELP}, transparency 0xc0c0c0;
   the captions "|OK" ... are "text|tooltip" (MCDialog::InitializeAutoTips). Apply is disabled at open
   (OnInitDialog EnableWindow(0x3021, 0)); batch 12: live, pp_apply_enabled / pp_apply below. Help not ported. */
const PpButton PP_BUTTONS[4] = { { PPF_OK, 1, 7003, 1, "OK" }, { PPF_CANCEL, 2, 7005, 1, "Cancel" },
    { PPF_APPLY, 12321, 7004, 1, "Apply" }, { PPF_HELP, 9, 7006, 0, "Help" } };
/* batch 12: Apply 12321 (decomp/b12/tpdlg_all.c). OnPageDataChanged 0x180893c70 (UWM from a page's SetPageDataChanged): state 1
   (changed) and this[0x729c] = 0 -> EnableWindow(0x3021, 1), and the page's tree item dirty; nothing disables it again but Apply.
   OnApplyPb 0x180892b20: ValidateActivePage (vtable +0x3e0) true -> this[0x734c] = 1, OnOK_Pages (+0x3d0: every made page's
   MapToOp, SetCannedTextFlag, the dialog's op copied to cur_op), SetAllPagesDataChanged(0) (every page clean: SetTreeClean),
   InitializeTreeStates (+0x400: the disabled images again), EnableWindow(0x3021, 0). The dialog stays open, and its Cancel still
   restores the op as it was at OnInitDialog (OnCancel 0x180892c50 -> RestoreOperation 0x180895be0, the copy at this +0x49c0): Apply
   changes nothing of the op by itself; who reads this[0x734c] is NOT DECODED (no reader in ToolpathDlg). Here the pages write the
   pending op as the user edits (pp_note_change marks the item dirty), so Apply = the tree clean and Apply disabled; "enabled" =
   some item dirty since the dialog opened / the last Apply (the same moments as Mastercam's EnableWindow). */
int pp_apply_enabled(void) { for (int s = 0; s < PG_N; s++) if (pp_tstate[s] == 1) return 1; return 0; }
void pp_apply(void) {
    if (!show_params || !pp_apply_enabled()) return;
    const CamParams *p = &pending.p;
    for (int s = 0; s < PG_N; s++) pp_tstate[s] = pg_has(p, s) && pp_disabled(p, s) ? 2 : 0;   /* SetAllPagesDataChanged(0) + InitializeTreeStates */
}
/* batch 12: Hide dialog 10320 = OnHideDialogPb 0x180892f20 (this[0x729c] = 0): GetCoreString(0x6c37) = mccoreres 27703 "Press <ESC>
   when done", AfxSuspendWindow(dialog, 0, 6) (the dialog hidden, the graphics usable), HistDlg_enable(1), get_key(prompt) until it
   reports a key / the loop flag drops, clearprompt, HistDlg_enable(0), AfxResumeWindow(0, 9). get_key's own rule (which keys end the
   wait) is NOT DECODED: the prompt's Esc ends it here (the front end's Esc chain), as the text says. */
int pp_hide_on;
void pp_hide(void) { if (!show_params) return; pp_hide_on = 1; snprintf(cam_msg, sizeof cam_msg, "Press <ESC> when done"); }
void pp_hide_end(void) { if (!pp_hide_on) return; pp_hide_on = 0; cam_msg[0] = 0; }   /* clearprompt, AfxResumeWindow */
/* the toolbar (InitToolBar + ToolpathDlg::AddDefaultsButtons 0x18088faa0, this[0x729c] = 0 images; tooltips mccoreres
   32932 / 32933 / 32935 / 32938 / 32939 / 403). Live (pp_tb_live / pp_tb_click): Tool Manager... (RunToolManager -> this
   program's Tool Manager), batch 12 Reload parameters from defaults file (pp_reload_defaults) and Hide dialog (pp_hide); bmp 0
   = a separator; hst: only for opcodes 0x84 / 0x86 / 0x87 (0x169f) */
const PpTbBtn PP_TB[PP_NTB] = { { 5699, "Tool Manager...", 0 }, { 0, 0, 0 }, { 9433, "Save parameters to defaults file", 0 },
    { 9431, "Reload parameters from defaults file", 0 }, { 0, 0, 1 }, { 9435, "Feed speed calculator...", 1 }, { 0, 0, 0 },
    { 10501, "Preview toolpath...", 0 }, { 10320, "Hide dialog", 0 } };
/* batch 12 (decomp/b12/tpdlg_all.c): greyed - Feed speed calculator 9435 (OnFeedSpeedCalcPb -> FeedCalculator 0x1808902b0 ->
   CalcFeedDialog, NOT DECODED; then UpdateFeedsSpeeds). Q19 / Q20 (formats/opdlg_port_audit.md 22): Save parameters to defaults file
   9433 (OnWriteToDefaultsPb 0x180894ed0 -> cui_defaults.c pp_save_defaults) and Preview toolpath 10501 (OnPreviewToolpathPb
   0x180893ea0 -> cui_preview.c) live. */
int pp_tb_live(int i) {
    if (i < 0 || i >= PP_NTB) return 0;
    switch (PP_TB[i].bmp) {
    case 5699: return cui_hooks.tool_manager != NULL;
    case 9431: return default_op(pending.p.type, mcam_is_metric(&F)) != NULL;   /* ReplaceOperation's defaults-file test */
    case 9433: return pp_save_defaults_live();                         /* Q19 (cui_defaults.c) */
    case 10501: return pv_live();                                      /* Q20 (cui_preview.c) */
    case 10320: return 1;
    }
    return 0;
}
void pp_tb_click(int i) {
    if (!pp_tb_live(i)) return;
    switch (PP_TB[i].bmp) {
    case 5699: cui_hooks.tool_manager(); break;
    case 9431: pp_reload_defaults(); break;
    case 9433: pp_save_defaults(); break;
    case 10501: pp_preview(); break;                   /* the Qt dialog runs pv_begin / gen_run / pv_end itself (a worker thread) */
    case 10320: pp_hide(); break;
    }
}
int pp_hst(const CamParams *p) { int t = p->type; return t == CAM_DYNAMIC || t == CAM_AREA || t == CAM_DYNCONTOUR || t == CAM_PEEL; }

/* the title (FUN_18026dc70, see the top of this file) */
void pp_title(char *out, size_t n) {
    const CamParams *p = &pending.p;
    int t = p->type, dyn = pp_hst(p);
    /* opcode 0x82 (the separate Tab Cutoff op): FUN_18026d220 case 0x82 = mccoreres 0x113f (4415) "Tab Cutoff" */
    const char *tname = t == CAM_CONTOUR && p->ct_cutoff_op ? "Tab Cutoff" : t == CAM_CONTOUR ? "Contour" : t == CAM_POCKET ? "Pocket" : t == CAM_FACE ? "Facing" : t == CAM_SLOT ? "Slot Mill"
        : t == CAM_CIRCLE ? "Circle Mill" : t == CAM_THREAD ? "Thread Mill" : t == CAM_HELIX ? "Helix Bore"
        : t == CAM_DYNAMIC ? "Dynamic Mill" : t == CAM_AREA ? "Area Mill" : t == CAM_DYNCONTOUR ? "Dynamic Contour" : t == CAM_PEEL ? "Peel Mill" : "";
    static const char *cyn[] = { "Drill/Counterbore", "Peck Drill", "Chip Break", "Tap", "Bore #1", "Bore #2", "Misc. #1", "Misc. #2" };
    if (t == CAM_DRILL) snprintf(out, n, "2D Toolpaths - Drill/Circles %s", p->drill_cycle >= 0 && p->drill_cycle < 8 ? cyn[p->drill_cycle] : "");
    else if (t == CAM_SRPOCKET) snprintf(out, n, "Surface Rough Pocket");     /* srough: mccoreres 4335 (the op 10 description; the sheet's
                                                                                 caption is NOT DECODED) */
    else if (t == CAM_SRPARALLEL) snprintf(out, n, "Surface Rough Parallel");
    else if (t == CAM_SRMPOCKET) snprintf(out, n, "Multisurface Rough Pocket");   /* mccoreres 4432 */
    else if (t == CAM_SRPLUNGE) snprintf(out, n, "Surface Rough Plunge");          /* mccoreres 4365 */
    else if (t == CAM_SFRADIAL) snprintf(out, n, "Surface Finish Radial");         /* mccoreres 4337 */
    else if (t == CAM_SFPENCIL) snprintf(out, n, "Surface Finish Pencil");        /* mccoreres 4360 */
    else if (t == CAM_SFPROJECT) snprintf(out, n, "Surface Finish Project");       /* mccoreres 4338 */  /* mccoreres 4330 */
    else if (t == CAM_SFCONTOUR) snprintf(out, n, "Surface Finish Contour");   /* mccoreres 4340 */
    else if (t == CAM_SRPROJECT) snprintf(out, n, "Surface Rough Project");    /* mccoreres 4332 */
    else if (t == CAM_BLEND) snprintf(out, n, "Surface Finish Blend");         /* the op's name as the other classic ops' (the sheet caption is NOT DECODED) */
    else if (t == CAM_ENGRAVE) snprintf(out, n, "Engraving");                  /* engraveres 8: the property sheet's caption */
    else if (t == CAM_WATERLINE) snprintf(out, n, "High Speed - Waterline");
    else if (t == CAM_RASTER) snprintf(out, n, "High Speed - Raster");   /* composed as OptiRough's (ours) */
    else if (t == CAM_AREAROUGH) snprintf(out, n, "High Speed - Area Roughing");
    else if (t == CAM_SPIRAL) snprintf(out, n, "High Speed - Spiral");
    else if (t == CAM_RADIAL) snprintf(out, n, "High Speed - Radial");
    else if (t == CAM_SCALLOP) snprintf(out, n, "High Speed - Scallop");
    else if (t == CAM_PENCIL) snprintf(out, n, "High Speed - Pencil");
    else if (t == CAM_HORIZAREA) snprintf(out, n, "High Speed - Horizontal Area");
    else if (t == CAM_HSTPROJECT) snprintf(out, n, "High Speed - Project");
    else if (t == CAM_OPTIROUGH) snprintf(out, n, "High Speed - Dynamic OptiRough");   /* 3drough: mcmsurfres 441 / 159; the title's
                                                                                       composition (mcmsurf) is NOT DECODED - joined as 2D's (ours) */
    else snprintf(out, n, "%s - %s", dyn ? "2D High Speed Toolpath" : "2D Toolpaths", tname);
}
/* the start of a frame (draw_op_params): the active page (ToolpathDlg::s_ActivePage, kept between dialogs); a page this
   program cannot show is never made active: ActiveNewPage 0x18088f870 falls back to SelectNewPage(s_ActivePage) when
   MakePage gives no page. A new dialog: the tree item images from IsPageDisabled (InitializeTreeStates). */
int pp_page_shown(void) { return pp_page == PG_AXIS ? PG_AXCOMBO : pp_page; }
void pp_frame_begin(void) {
    CamParams *p = &pending.p;
    if (!pg_has(p, pp_page) || !pg_done(p, pp_page == PG_AXIS ? PG_AXCOMBO : pp_page)) pp_page = PG_CUT;
    if (pp_tree_fresh) { for (int s = 0; s < PG_N; s++) pp_tstate[s] = pg_has(p, s) && pp_disabled(p, s) ? 2 : 0; pp_mp_pic = 0; pp_tree_fresh = 0; }
}
/* the tree 25455 (FUN_18014a700): the rows in order with their depth; image index 0 clean, 1 dirty, 2 disabled */
int pp_tree_rows(PpTreeRow *out, int cap) {
    const CamParams *p = &pending.p; int n = 0;
    for (int i = 0; i < (int)(sizeof PG_TREE / sizeof PG_TREE[0]) && n < cap; i++) {
        if (!pg_parent_ok(p, i)) continue;
        int s = PG_TREE[i].s, depth = 0; for (int q = PG_TREE[i].parent; q; depth++) { int pq = 0; for (int k = 0; k < i; k++) if (PG_TREE[k].s == q && pg_parent_ok(p, k)) pq = PG_TREE[k].parent; q = pq; }
        out[n].slot = s; out[n].depth = depth; out[n].done = pg_done(p, s == PG_AXIS ? PG_AXCOMBO : s); out[n].state = pp_tstate[s]; out[n].title = PG_TITLE[s];
        if (p->type == CAM_SRPOCKET || p->type == CAM_SRPARALLEL || p->type == CAM_SRMPOCKET)   /* srough: the sheet's tab captions (the templates' titles; op 131's tab 4 = 1312, title string 147 as 242's) */
            out[n].title = s == PG_TOOL ? "Tool parameters" : s == PG_CUT ? "Surface parameters" : s == PG_ROUGH ? (p->type == CAM_SRPOCKET || p->type == CAM_SRMPOCKET ? "Rough parameters" : "Rough parallel parameters")
                         : s == PG_FINISH ? "Pocket parameters" : PG_TITLE[s];
        if (p->type == CAM_SFPENCIL) out[n].title = s == PG_TOOL ? "Tool parameters" : s == PG_CUT ? "Surface parameters" : s == PG_ROUGH ? "Finish pencil parameters" : PG_TITLE[s];   /* tab string 131 */
        if (p->type == CAM_SFPROJECT) out[n].title = s == PG_TOOL ? "Tool parameters" : s == PG_CUT ? "Surface parameters" : s == PG_ROUGH ? "Project parameters" : PG_TITLE[s];
        if (p->type == CAM_SFRADIAL) out[n].title = s == PG_TOOL ? "Tool parameters" : s == PG_CUT ? "Surface parameters" : s == PG_ROUGH ? "Radial parameters" : PG_TITLE[s];   /* the template's title */
        if (p->type == CAM_SRPLUNGE) out[n].title = s == PG_TOOL ? "Tool parameters" : s == PG_CUT ? "Surface parameters" : s == PG_ROUGH ? "Rough plunge parameters" : PG_TITLE[s];   /* tab string 125 */
        if (p->type == CAM_BLEND) out[n].title = s == PG_TOOL ? "Tool parameters" : s == PG_CUT ? "Surface parameters" : s == PG_ROUGH ? "Finish blend parameters" : PG_TITLE[s];   /* tab string 142 */
        if (p->type == CAM_SRPROJECT) out[n].title = s == PG_TOOL ? "Tool parameters" : s == PG_CUT ? "Surface parameters" : s == PG_ROUGH ? "Rough project parameters" : PG_TITLE[s];   /* tab string 138 */
        if (p->type == CAM_SFCONTOUR) out[n].title = s == PG_TOOL ? "Tool parameters" : s == PG_CUT ? "Surface parameters" : s == PG_ROUGH ? "Contour parameters" : PG_TITLE[s];   /* the templates' titles */
        if (p->type == CAM_ENGRAVE) out[n].title = s == PG_TOOL ? "Toolpath parameters" : s == PG_CUT ? "Engraving parameters" : s == PG_ROUGH ? "Roughing/Finishing" : PG_TITLE[s];   /* the templates' titles (engraveres 1802 / 1803) */
        if (p->type == CAM_SCALLOP && s == PG_ROUGH) out[n].title = "Steep / Shallow";   /* ours: the template's title is "HST Scallop Steep Shallow" */
        n++;
    }
    return n;
}
/* a click on a tree item: a page this program does not fill keeps the selection (ActiveNewPage, above) */
void pp_tree_click(int slot) {
    if (slot > 0 && slot < PG_N && pg_has(&pending.p, slot) && pg_done(&pending.p, slot == PG_AXIS ? PG_AXCOMBO : slot)) {
        pp_page = slot;   /* the Planes page: InitPage's RefreshData (Manual = Tplane +0x92 >= 0) holds already for the op as opened (MapToOp / the loader) */
    }
}
/* ToolpathPageBase::SetPageDataChanged 0x180899190 (1) from the page's handlers -> OnPageDataChanged 0x180893c70:
   SetTreeDirty on the page's item; here: the page changed the op while the user was typing / clicking */
void pp_note_change(const CamParams *before, const OpHFlags *hbefore) {
    if (memcmp(before, &pending.p, sizeof *before) || memcmp(hbefore, &pp_hf, sizeof *hbefore)) pp_tstate[pp_page] = 1;
}
/* Quick View Settings (ToolpathDlg::InitQuickViewList 0x1808917e0: 2 columns 45 % / 53 %, name + QuickViewData::
   GetDisplayString 0x1805ee500 = "%d" / "%ld" / "%g" / On 5130 / Off 5126 / the string). Items in slot order from the
   pages' GetCustomizableData: ToolPageBase 0x1808840e0 (0 Tool 7517 = the tool name, 1 Tool Diameter 7532, 2 Corner Radius
   7533, 3 Feed Rate 7518, 4 Spindle Speed 7537, 7 Length Offset 7543, 8 Diameter Offset 7544; 6 Tool Length 7542 NOT
   DECODED - left out), CoolantPageBase 0x180232ef0 (5 Coolant 7538, any coolant on), PlanesPageBase 0x1805c4b90 (9 Cplane /
   Tplane 7545: GetPlaneName of the Tplane, op +0x5d8; PlanesPageBase::RefreshData 0x1805c69e0 (InitPage) then sets row 9
   to the Cplane's name this +0xea20 when it differs - the value shown), HSTDrillTipCompPage FUN_180165190 (13 Tip comp mcmillres 319, 2287 / 2288). */
int pp_quickview_rows(const char **nm, char (*v)[64]) {
    const CamParams *p = &pending.p;
    const Tool *tl = tool_choice >= 0 && tool_choice < F.ntools ? &F.tools[tool_choice] : NULL;
    int n = 0;
    #define QV(N_, ...) do { nm[n] = N_; snprintf(v[n], 64, __VA_ARGS__); n++; } while (0)
    QV("Tool", "%s", tl ? tl->name : ""); QV("Tool Diameter", "%g", p->tool_dia); QV("Corner Radius", "%g", tl ? tl->corner_r : 0.0);
    QV("Feed Rate", "%g", p->feed); QV("Spindle Speed", "%ld", (long)p->rpm); QV("Coolant", "%s", pv_qv_coolant() ? "On" : "Off");   /* batch 11: IsCoolantEnabled */
    QV("Length Offset", "%d", tl ? tl->len_off : 0); QV("Diameter Offset", "%d", tl ? tl->dia_off : 0); QV("Cplane / Tplane", "%s", plane_name(p->cplane));   /* RefreshData's rewrite (cui_planes.c) */
    if (p->type == CAM_DRILL) QV("Tip comp", "%s", p->tip_comp ? "On" : "Off");
    #undef QV
    return n;
}
/* Cancel / Esc (mcamui: the Cancel button and the Esc key): an edit drops its copy; the op dialog and the chaining end */
void pp_cancel(void) {
    if (ms_dialog_cancel()) return;                      /* S4: a new 3D op's Cancel - the 427 question (cui_chain.c) */
    pv_cancel();                                         /* Q20: OnCancel after a preview - the op dirty (cui_preview.c) */
    pp_hide_on = 0;                                      /* batch 12 (Hide dialog) */
    if (edit_camop >= 0) { pending_free(); edit_camop = -1; }
    show_params = 0; cam_mode = 0; cam_clear_selection();
    pls_open = 0; upl_open = 0;                          /* the Planes page's modal dialogs go with it (ours) */
}

/* ---- the pages' controls (was opparams_ui.inc draw_cut_page and draw_op_params' page switch). Walked through the
   front end's PpOps in the nuklear layout order: row() starts a row (h in px, or in dialog units when du), the
   controls fill its cells; a control's op returns the value now shown (the user's change) and the model takes it at
   once, as the immediate-mode original did. Sources in the comments. */
#define P_ROW(H_, C_) ops->row(u, H_, 0, C_)
static void pp_prop(const PpOps *ops, void *u, const char *label, double *v, double lo, double hi, double step) {
    double n = ops->prop(u, label, *v, lo, hi, step); if (n != *v) *v = n;
}
static void pp_iprop(const PpOps *ops, void *u, const char *label, int *v, int lo, int hi) { int n = ops->iprop(u, label, *v, lo, hi); if (n != *v) *v = n; }
/* Cut Parameters page of each op type (the fields as before, sources in their comments) */
static void pp_cut_page(const PpOps *ops, void *u, CamParams *p) {
    int t = p->type, dyn = t == CAM_DYNAMIC || t == CAM_AREA || t == CAM_DYNCONTOUR || t == CAM_PEEL;
    int holes = t == CAM_HELIX || t == CAM_CIRCLE || t == CAM_THREAD;
    if (t == CAM_DRILL) {
        /* Drill Cut parameters (mcmillres dialog 25595): Cycle (mccoreres 7645 + 11 n), 1st peck (op +0x1230).
           Drill/Counterbore (G81), Peck Drill (G83) and Chip Break (G73) are posted here; other stored cycles show their name. */
        P_ROW(24, 2); ops->label(u, "Cycle", 0);
        if (p->drill_cycle >= 0 && p->drill_cycle <= 2) {
            static const char *cy[] = { "Drill/Counterbore", "Peck Drill", "Chip Break" };
            int c = ops->combo(u, cy, 3, p->drill_cycle, 200, 100);
            if (c != p->drill_cycle) { p->drill_cycle = c; if (!c) p->peck = 0; }
        } else { static const char *cn[] = { "Drill/Counterbore", "Peck Drill", "Chip Break", "Tap", "Bore #1", "Bore #2", "Misc. #1", "Misc. #2" };
            char l[64]; snprintf(l, sizeof l, "%s (not posted)", p->drill_cycle < 8 ? cn[p->drill_cycle] : "Custom cycle"); ops->label(u, l, 0); }
        P_ROW(22, 1);
        if (p->drill_cycle == 1 || p->drill_cycle == 2) pp_prop(ops, u, "#1st peck", &p->peck, 0, 100, 0.1);
    }
    P_ROW(22, 1);
    if (t != CAM_DRILL && t != CAM_FACE && t != CAM_THREAD && !(t == CAM_CONTOUR && p->contour_type)) pp_prop(ops, u, "#Stock to leave on walls", &p->stock_xy, -10, 10, 0.05);
    if ((t == CAM_CONTOUR && !p->contour_type) || t == CAM_POCKET || t == CAM_SLOT) {
        /* static 2908 beside the walls / floors stock fields: SetStockToLeaveBmp (walls, floors = record 0x888) */
        char nm[40]; snprintf(nm, sizeof nm, "mcmillres_BMPK_%d.png", pp_stock_bmp(p->stock_xy, pp_hf.fstock));
        ops->row_pic(u, 43, 4, 66, nm);
        P_ROW(22, 1);
    }
    if (t == CAM_POCKET) {
        /* Machining direction 25256: Climb 25113 / Conventional 25127 (DDX_Radio +0x72a0 = op +0x11d2 == 0, record 0x123a; not
           ported - greyed) and its picture 1358: FUN_18017bf60 0x648e climb / 0x648f conventional */
        int conv = pp_rec ? !PP_RB(0x123a) : 0, c0 = !conv, c1 = conv;
        P_ROW(20, 2); ops->option(u, "Climb", c0); ops->option(u, "Conventional", c1);
        char nm[40]; snprintf(nm, sizeof nm, "mcmillres_BMP_%d.png", conv ? 25743 : 25742);
        ops->row_pic(u, 34, 4, 71, nm);
        P_ROW(22, 1);
    }
    if (t == CAM_CONTOUR) {
        /* Contour Cut parameters (dialog 2938): Contour type (mccoreres 4561 "2D" / 4565 "2D chamfer"), Compensation type
           (4589 "Computer" / 4597 "Off"; Control / Wear / Reverse Wear not offered - the LinuxCNC post has no G41/G42),
           Compensation direction (4577 "Left" / 4579 "Right" of each chain's direction = side_rel) */
        P_ROW(24, 2);
        if (p->contour_type == 2) { ops->label(u, "Contour type:", 0); ops->label(u, "Engrave", 0); }
        else { static const char *ctypes[] = { "2D", "2D chamfer" }; ops->label(u, "Contour type", 0);
               p->contour_type = ops->combo(u, ctypes, 2, p->contour_type, 200, 80); }
        if (p->contour_type != 2) {
            static const char *ct[] = { "Computer", "Off" }, *cd[] = { "Left", "Right" };
            int off = p->side == SIDE_ON; ops->label(u, "Compensation type", 0);
            int noff = ops->combo(u, ct, 2, off, 200, 80);
            int dir = p->side == 2 ? 1 : 0;
            if (!noff) { ops->label(u, "Compensation direction", 0); dir = ops->combo(u, cd, 2, dir, 200, 80); }
            int ns = noff ? SIDE_ON : dir ? 2 : 1;
            if (ns != p->side || noff != off) { p->side = ns; p->side_rel = 1; }
            /* static 1358 beside the compensation combos: FUN_180158db0 - type 4 (Off) 0xbc, else direction Left 0xb2 /
               Right 0xb5 (UIBitmap, mcmillres) */
            char nm[40]; snprintf(nm, sizeof nm, "mcmillres_BMP_%d.png", p->side == SIDE_ON ? 188 : p->side == 2 ? 181 : 178);
            ops->row(u, 19, 1, 2); ops->spacing(u); ops->pic(u, 19, 17, nm);
            P_ROW(24, 2);
        }
        P_ROW(22, 1);
        if (p->contour_type == 1) {
            pp_prop(ops, u, "#Chamfer width:", &p->chamfer_w, 0.01, 50, 0.05);
            pp_prop(ops, u, "#Tip offset:", &p->chamfer_tip, 0, 20, 0.05);
            pp_prop(ops, u, "#Tool tip diameter:", &p->tip_dia, 0, 50, 0.05);
            double re, zt; char err[160];
            if (chamfer_preview(p, &re, &zt, err, sizeof err)) { char l[160]; snprintf(l, sizeof l, "tip at Z%.3f, centre %.3f from the chain", zt, re); ops->label(u, l, 0); }
            else ops->label(u, err, 1);
        }
    }
    if (t == CAM_FACE) {
        static const char *fs[] = { "Zigzag", "One way", "One pass", "Dynamic" }; static const int fv[] = { 0, 4, 3, 5 };   /* FUN_18016ba50 */
        int fi = p->face_style == 0 ? 0 : p->face_style == 3 ? 2 : p->face_style == 5 ? 3 : 1;
        P_ROW(24, 1); p->face_style = fv[ops->combo(u, fs, 4, fi, 380, 120)];
        {   /* static 1360 (147 x 66) under the style combo: FUN_18016ba50 / FUN_18016caf0 by the style (op +0x11c0): 0 0x6499,
               3 0x649b, 5 0x649c, else 0x649a */
            int fid = p->face_style == 0 ? 25753 : p->face_style == 3 ? 25755 : p->face_style == 5 ? 25756 : 25754;
            char nm[40]; snprintf(nm, sizeof nm, "mcmillres_BMP_%d.png", fid);
            ops->row_static_pic(u, 66, 147, nm);
        }
        P_ROW(22, 1);
        /* Face Cut parameters (dialog 2942): Style (mcmillres 2675-2679), Max. stepover [%] (+0x1240), overlaps */
        pp_prop(ops, u, "#Max. stepover [%]", &p->stepover_pct, 5, 95, 1);
        pp_prop(ops, u, "#Across overlap [%]", &p->face_overlap, 0, 100, 5);
        pp_prop(ops, u, "#Along overlap [%]", &p->face_along, 0, 300, 5);
        pp_prop(ops, u, "#Roughing angle:", &p->face_angle, -180, 180, 5);
        if (p->use_box) { char l[160]; snprintf(l, sizeof l, "stock box %.2f x %.2f", p->box[2] - p->box[0], p->box[3] - p->box[1]); ops->label(u, l, 0); }
    }
    if (dyn) {
        /* 2D High Speed Cut parameters (mcmillres dialog 2784 / ui_fields "Dynamic Mill Cut Parameters"), Machining region
           strategy (dialog 657, op +0x1567: 1 = From outside) */
        pp_prop(ops, u, "#Stepover [%]", &p->stepover_pct, 2, 90, 1);
        pp_prop(ops, u, "#Min toolpath radius [%]", &p->min_rad_pct, 0, 100, 1);
        pp_prop(ops, u, "#Micro lift distance", &p->micro_lift, 0, 5, 0.05);
        pp_prop(ops, u, "#Back feedrate", &p->back_feed, 0, 50000, 50);
        pp_prop(ops, u, "#Gap size, retract: Distance", &p->link_retract, 0, 1000, 1);
        if (t != CAM_DYNAMIC) pp_prop(ops, u, "#Plunge angle", &p->ramp_deg, 0.5, 30, 0.5);
        if (t == CAM_DYNAMIC) {
            static const char *mr[] = { "Stay inside", "From outside" };
            P_ROW(24, 2); ops->label(u, "Machining region strategy", 0);
            p->dyn_outside = ops->combo(u, mr, 2, p->dyn_outside ? 1 : 0, 200, 80);
            P_ROW(22, 1);
        }
        if (t == CAM_DYNCONTOUR) {   /* NOT DECODED: Dynamic Contour wall settings (our own controls) */
            pp_prop(ops, u, "#Wall stock width:", &p->wall_stock, 0.1, 200, 0.5);
            static const char *ds[] = { "Outside the chain", "Inside the chain" };
            P_ROW(24, 1);
            int si = p->side == SIDE_INSIDE; si = ops->combo(u, ds, 2, si, 380, 80); p->side = si ? SIDE_INSIDE : SIDE_OUTSIDE;
        }
        if (t == CAM_PEEL) pp_prop(ops, u, "#Slot width", &p->slot_width, 0, 500, 0.5);   /* dialog 2784 618 (record 0x1258, which the Peel generator reads; 0 = 4 x D, PreparePockBoundsPeel l.6633-6636) */
    }
    if (t == CAM_CIRCLE) pp_prop(ops, u, "#Helix pitch (depth / rev):", &p->helix_pitch, 0.01, 50, 0.1);   /* circle mill page not mapped */
    if (t == CAM_THREAD) {
        /* Thread Mill Cut parameters (mcmillres dialog 2824; the template page is the one shown): the CamParams gen_thread reads
           (threadmill: the ours "major diameter" / "thread depth" / "full profile" rows removed - never read) */
        pp_prop(ops, u, "#Thread pitch", &p->thread_pitch, 0, 20, 0.05);
        P_ROW(22, 2);
        { static const char *it[] = { "OD thread", "ID thread" }; p->thread_internal = ops->combo(u, it, 2, p->thread_internal ? 1 : 0, 200, 80); }
        { static const char *hd[] = { "Left-hand thread", "Right-hand thread" }; p->thread_rh = ops->combo(u, hd, 2, p->thread_rh ? 1 : 0, 200, 80); }
        ops->label(u, "Machining direction", 0);
        { static const char *md[] = { "Top to bottom", "Bottom to top" }; p->thread_up = ops->combo(u, md, 2, p->thread_up ? 1 : 0, 200, 80); }
    }
    if (holes) {
        char l[200]; int n = 0; double dmin = 1e300, dmax = 0;
        for (int i = 0; i < pending.nholes; i++) if (pending.holes[i].d > 0) { n++; dmin = fmin(dmin, pending.holes[i].d); dmax = fmax(dmax, pending.holes[i].d); }
        if (n && dmax > dmin) snprintf(l, sizeof l, "%d circle(s), diameter %.3f - %.3f", n, dmin, dmax);
        else if (n) snprintf(l, sizeof l, "%d circle(s), diameter %.3f", n, dmin);
        else snprintf(l, sizeof l, "0 points defined!");   /* mcmillres 2397 (tp_pt_mgr warning) */
        P_ROW(20, 1); ops->label(u, l, 0);
    }
}
/* the controls of page PG (pp_page_shown(); not the Tool page or the Linking page, which have their own models): the
   row-laid pages - since 2026-10-22 only those without a template (Coolant, Planes); the others are template pages
   (pf_ctls below) and these cases stay as the fallback */
void pp_page_body(int pg, const PpOps *ops, void *u) {
    CamParams *p = &pending.p; int t = p->type, dyn = pp_hst(p);
    switch (pg) {
    case PG_DEPTH: P_ROW(22, 1); pp_prop(ops, u, "#Max rough step:", &p->stepdown, 0.01, 1000, 0.1); break;
    case PG_LEAD: P_ROW(22, 1);
        pp_prop(ops, u, "#Lead arc radius (0 = none):", &p->lead_r, 0, 50, 0.25); pp_prop(ops, u, "#Lead line length:", &p->lead_len, 0, 50, 0.25); break;
    case PG_MULTI:
        P_ROW(22, 1);
        if (t == CAM_THREAD) break;   /* threadmill: the template 2825 (the record's multipass fields); "Radial passes" (ours) removed */
        /* Multi Passes page (mcmillres dialog 25591) group Finish - Number / Spacing */
        { int fn = ops->iprop(u, "#Finish: Number", p->finish_n, 0, 10); if ((fn > 0) != (p->finish_n > 0)) pp_mp_pic = fn > 0; p->finish_n = fn; }
        if (p->finish_n) pp_prop(ops, u, "#Spacing", &p->finish_step, 0.01, 10, 0.05);
        break;
    case PG_TABS: P_ROW(22, 1);
        pp_iprop(ops, u, "#Number of tabs", &p->tabs, 0, 12);
        if (p->tabs) { pp_prop(ops, u, "#Width", &p->tab_w, 0.1, 100, 0.5); pp_prop(ops, u, "#Height", &p->tab_h, 0.05, 50, 0.1); }
        break;
    case PG_ROUGH: P_ROW(22, 1);   /* Pocket Rough parameters (dialog 3003) */
        pp_prop(ops, u, t == CAM_POCKET ? "#Stepover percentage" : "#Stepover % of D:", &p->stepover_pct, 5, 95, 1);
        if (t == CAM_CIRCLE) pp_prop(ops, u, "#Start circle radius (0 = auto):", &p->entry_r, 0, 100, 0.1);
        break;
    case PG_ENTRY: P_ROW(22, 1);   /* Entry - helix (dialog 3010) / Dynamic Entry Motion */
        pp_prop(ops, u, "#Plunge angle", &p->ramp_deg, 0.5, 90, 0.5);
        if (dyn) pp_prop(ops, u, "#Helix radius", &p->entry_r, 0, 100, 0.1);
        break;
    case PG_FINISH: P_ROW(22, 1);  /* Pocket Finish parameters (dialog 3004) */
        pp_iprop(ops, u, t == CAM_POCKET ? "#Finish: Passes" : "#Finish passes", &p->finish_n, 0, 10);
        if (p->finish_n) pp_prop(ops, u, "#Spacing", &p->finish_step, 0.01, 10, 0.05);
        break;
    case PG_ROUGHFIN: P_ROW(22, 1);
        if (t == CAM_SLOT) { pp_prop(ops, u, "#Stepover [%]", &p->stepover_pct, 5, 95, 1); pp_prop(ops, u, "#Plunge angle", &p->ramp_deg, 0.5, 90, 0.5); }   /* dialog 3020 */
        else {                                       /* Helixbore Cut parameters (dialog 2823) */
            pp_prop(ops, u, "#Rough pitch", &p->helix_pitch, 0.01, 50, 0.1);
            pp_iprop(ops, u, "#Finish", &p->finish_n, 0, 1);
            if (p->finish_n) pp_prop(ops, u, "#Finish stepover", &p->finish_step, 0.01, 10, 0.05);
        }
        break;
    case PG_TIP: P_ROW(22, 1);
        { int tc = p->tip_comp, n = ops->check(u, "Tip comp", tc); if (n != tc) p->tip_comp = n; }   /* mcmillres control 1310 */
        break;
    case PG_ARC: P_ROW(22, 1);    /* row-laid fallback (the page is template mccoreres 10318 ArcFilterRefinePage, ctor arg 0x284e;
                                     "Create arcs in XY" is ours: CamParams.arcs, the old mcmillres 318 label) */
        { int a = p->arcs, n = ops->check(u, "Create arcs in XY", a); if (n != a) p->arcs = n; }
        break;
    case PG_COOL: P_ROW(24, 2);   /* Coolant page: Flood Off / On (mcmillres 126 / 124 / 2287) - batch 11: unused, the template page 6546 replaces it */
        ops->label(u, "Flood", 0);
        { static const char *fl[] = { "Off", "On" }; p->coolant = ops->combo(u, fl, 2, p->coolant ? 1 : 0, 200, 80); }
        break;
    case PG_CUT: pp_cut_page(ops, u, p); break;
    case PG_PLANES: planes_page_body(ops, u); break;  /* dialog 6547 (core_ui cui_planes.c) */
    }
}
#undef P_ROW
/* a page with a picture static: the picture at its template rectangle, this program's row-laid controls in a group
   beside it (left picture: right of it; right picture: left of it; picture low on the page: above it); dialog units
   inside the page frame (435 x 295) */
PpRect pp_wrap_rect(int x, int y, int w, int h) {
    (void)h;
    if (x < 150 && y < 100) return (PpRect){ x + w + 6, 0, 435 - x - w - 6, 295 };
    if (y >= 100 && x < 200) return (PpRect){ 0, 0, 435, y - 2 };
    return (PpRect){ 0, 0, x - 4, 295 };
}

/* ================================================================================================ template pages
   (cui_opfields.h; formats/mcamui_dialogs.md 1 "Template pages"). The pages below are drawn from their Mastercam
   templates: every control of the template at its position (exports/mcmillres_dialogs.tsv, mccoreres for 10318 / 1343),
   joined with its decoded record field (formats/opdlg_fields.tsv; tools/gen_opdlg_fields.py -> cui_opfields.inc, which
   carries each row's decompile citation). A control shows the op's record value: the dialog's edits, else the op's
   record patches (rpatch), else the record the dialog was seeded from (pp_rec: the stored op) or, for an op made here,
   the Mill_MM / Mill_Inch defaults op it is saved from (cam_default_op_record; mcam_saveops.c copies it). Live are:
   (1) the bound controls (pf_bget / pf_bset): the CamParams values this program's generators use, with the ranges and
   rules of the row-laid controls they replace; (2) the controls whose record field an engine reads from its record block
   (pocket: cam2d_pock.c pkb / pkd / pks on pk_p; slot / helix / circle / thread: cam2d_ops.c mcb / mcd / mcs on mc_p),
   with the template's enable rule when it is a group / check box ("buddy of N"), greyed for any other rule. The rest is
   greyed (decoded, not used here) or greyed + NOT DECODED. Edits are written back at OK as record patches (pf_apply,
   only the edited fields: an untouched op saves byte-identically) and reapplied to the engine blocks when an operations
   sidecar is read again (cam_fileop.c cam_params_from_patches). Controls this program adds are "ours" (pf_ours). */
typedef struct {
    int dlg, id, kind, x, y, w, h; const char *text, *label; uint16_t off[6], opc[6];
    uint8_t type, val; int8_t k; uint8_t st; const char *items; int en, nd;
} PfDef;
enum { PFT_F64 = 1, PFT_U8, PFT_S16, PFT_S32 };                              /* record field types (gen_opdlg_fields.py TYPES) */
enum { PFV_NONE, PFV_NUM, PFV_BOOL, PFV_INV, PFV_EQ, PFV_COMBO, PFV_CUSTOM };   /* value modes (V_*) */
enum { PFS_DEC, PFS_PART, PFS_ND, PFS_NOFIELD, PFS_H };                       /* tsv status */
static const PfDef PF_DEF[] = {
#include "cui_opfields.inc"
};
static int pf_tsize(int t) { return t == PFT_F64 ? 8 : t == PFT_U8 ? 1 : t == PFT_S16 ? 2 : t == PFT_S32 ? 4 : 0; }
/* the page's template dialog per slot (audit table 1; MakeSpecificPages x the page factories, decomp/mcmill_all.c) */
static int pf_dlg(const CamParams *p, int s) {
    int t = p->type, hst = pp_hst(p);
    switch (s) {
    case PG_CUT: return t == CAM_ENGRAVE ? 1802 : t == CAM_SRPOCKET || t == CAM_SRMPOCKET || t == CAM_SRPARALLEL || t == CAM_SFCONTOUR || t == CAM_SRPROJECT || t == CAM_BLEND || t == CAM_SRPLUNGE || t == CAM_SFRADIAL || t == CAM_SFPROJECT || t == CAM_SFPENCIL ? 999 : t == CAM_WATERLINE ? 1324 : t == CAM_RASTER ? 1330 : t == CAM_AREAROUGH ? 1326 : t == CAM_SPIRAL ? 1338 : t == CAM_RADIAL ? 1339 : t == CAM_SCALLOP ? 1328 : t == CAM_PENCIL ? 1329 : t == CAM_HORIZAREA ? 1327 : t == CAM_HSTPROJECT ? 1517 : t == CAM_OPTIROUGH ? 1465 : t == CAM_CONTOUR && p->ct_cutoff_op ? 3025 : t == CAM_CONTOUR ? 2938 : t == CAM_POCKET ? 2940 : t == CAM_FACE ? 2942 : t == CAM_SLOT ? 2944
        : t == CAM_DRILL ? 25595 : t == CAM_CIRCLE || t == CAM_HELIX ? 25592 : t == CAM_THREAD ? 2824
        : t == CAM_AREA ? 379 : t == CAM_PEEL ? 2784 : hst ? 3205 : 0;   /* 379 HSTCoreRoughPage, 2784 HSTPeelTrochoidalPage, 3205 styles 0 / 2 */
    case PG_DEPTH: return t == CAM_POCKET ? 3030 : hst ? 25602 : 25598;
    case PG_FINPASS: return 2833;
    case PG_TROCH: return 1343;                                     /* mccoreres 1343 (HSTTrochoidalMotionPage ctor arg 0x53f) */
    case PG_TRANS: return t == CAM_OPTIROUGH ? 1430 : t == CAM_WATERLINE ? 1341 : 2826;   /* Waterline: HSTWaterlineTransitionsPage 0x53d */           /* 3drough: HSTOptRoughTransitionsPage (mcmsurfres 1430) */
    case PG_LEAD: return t == CAM_POCKET ? 2793 : t == CAM_CIRCLE ? 3271 : t == CAM_THREAD ? 2969 : 3013;
    case PG_BREAK: return 25599;
    case PG_LINK: return t == CAM_THREAD ? 2837 : 0;                /* HSTThreadmillLinkPage (ctor arg 0xb15); the others: lk_rows */
    case PG_ARC: return 10318;                                      /* mccoreres 10318 ArcFilterRefinePage (ctor arg 0x284e) */
    case PG_MULTI: return t == CAM_THREAD ? 2825 : 25591;
    case PG_TIP: return 25594;
    case PG_TABS: return 2834;
    case PG_TABCUT: return 3022;
    case PG_ROUGH: return t == CAM_ENGRAVE ? 1803 : t == CAM_SFPENCIL ? 240 : t == CAM_SRPROJECT ? 1037 : t == CAM_BLEND ? 1040 : t == CAM_SRPLUNGE ? 1038 : t == CAM_SFRADIAL ? 1036 : t == CAM_SFPROJECT ? 1002 : t == CAM_SFCONTOUR ? 180 : t == CAM_SCALLOP ? 1342 : t == CAM_SRPOCKET || t == CAM_SRMPOCKET ? 1310 : t == CAM_SRPARALLEL ? 1039 : t == CAM_CIRCLE ? 25597 : 3003;   /* srough: 1310 / 1039 */
    case PG_FINISH: return t == CAM_SRPOCKET ? 242 : t == CAM_SRMPOCKET ? 1312 : t == CAM_CIRCLE ? 3270 : 3004;   /* srough: 242 Pocket parameters */
    case PG_ROUGHFIN: return t == CAM_HELIX ? 2823 : 3020;
    case PG_ENTRY: return t == CAM_POCKET ? 3005 : 3219;
    case PG_WALL: return 3299;
    case PG_FINISH2: return 3301;
    case PG_HSTLEAD: return 25603;
    case PG_STOCK: return 1337;
    /* batch 11: the post-side pages (mccoreres templates; ctor args CoolantPage 0x1992 / CannedTextPage 0x1994 / ReferencePointsPage
       0x199a / MiscellaneousValuesPage 0x199b, decomp/opaudit_mccore_ctors.c) */
    case PG_COOL: return 6546;
    case PG_CANNED: return 6548;
    case PG_HOME: return 6554;
    case PG_MISC: return 6555;
    }
    return 0;
}
int pf_page_dialog(int slot) { return pg_has(&pending.p, slot) ? pf_dlg(&pending.p, slot) : 0; }
int pf_active(int slot) { return pf_page_dialog(slot) != 0; }

/* ---- the record values */
/* this dialog's record edits: room for every live field of one op type with its partners (< 200 distinct record offsets
   per type in cui_opfields.inc); a full list refuses the edit with a message, never drops it quietly */
#define PF_NED 256
static struct { uint16_t off; uint8_t n; uint8_t v[8]; } pf_ed[PF_NED]; static int pf_ned;
/* Tab Cutoff 3022 (HSTTabSettingsPage): the radio member +0x1658 FUN_1801918c0 tests (1150 enabled when it is 2 and the group is
   checked): MapToDlg FUN_180191570 sets it to the index (mode - 1), the radio handlers FUN_180191710 / 180191740 / 1801917b0 to 1 / 2 /
   3 (All / Each / Separate) - so on open "Separate" enables it, after a click "After each". pf_tc_click: the last click (0 none) */
static int pf_tc_click, pf_tc_last = 1;
void pf_reset(void) { pf_ned = 0; pf_tc_click = 0; }
int pf_dirty_sig(unsigned char *out, int cap) { int n = (int)sizeof pf_ed[0] * pf_ned; if (n > cap) n = cap; memcpy(out, pf_ed, n); return n; }
static int pf_opcode(void) { int st; return mcam_opcode_for(&pending.p, &st); }
static int pf_style(void) { int t = pending.p.type; return t == CAM_DYNAMIC ? 0 : t == CAM_AREA ? 1 : t == CAM_DYNCONTOUR ? 2 : t == CAM_PEEL ? 3 : -1; }
static int pf_off(const PfDef *d) {                       /* the control's record offset for this op's opcode, -1 none */
    int oc = pf_opcode(), any = -1;
    for (int i = 0; i < 6 && d->off[i]; i++) { if (d->opc[i] == oc) return d->off[i]; if (!d->opc[i]) any = d->off[i]; }
    return any;
}
static const uint8_t *pf_src(uint32_t *n) {
    if (pp_rec) { *n = pp_rec_n; return pp_rec; }
    return cam_default_op_record(pending.p.type, mcam_is_metric(&F), n);
}
static int pf_raw(int off, int n, uint8_t *v) {
    for (int k = pf_ned - 1; k >= 0; k--) if (pf_ed[k].off == off && pf_ed[k].n == n) { memcpy(v, pf_ed[k].v, n); return 1; }
    const CamParams *p = &pending.p;
    for (int k = 0; k < p->nrpatch; k++) if (p->rpatch[k].off == off && p->rpatch[k].n == n) { memcpy(v, p->rpatch[k].v, n); return 1; }
    uint32_t rn = 0; const uint8_t *r = pf_src(&rn);
    if (r && (uint32_t)off + n <= rn) { memcpy(v, r + off, n); return 1; }
    memset(v, 0, n); return 0;
}
static int pf_has_patch(int off);
static void cd_dir_put(CamParams *p, int right);
static double pf_rv(int off, int type) {
    uint8_t b[8]; int n = pf_tsize(type); if (off < 0 || !n) return 0;
    pf_raw(off, n, b);
    switch (type) { case PFT_F64: { double d; memcpy(&d, b, 8); return d; } case PFT_U8: return b[0];
        case PFT_S16: { int16_t s; memcpy(&s, b, 2); return s; } case PFT_S32: { int32_t s; memcpy(&s, b, 4); return s; } }
    return 0;
}
/* an edit of record field OFF: kept for pf_apply, and poked into the engine's record block when it has one */
static void pf_put(int off, int type, double v) {
    uint8_t b[8]; int n = pf_tsize(type); if (off < 0 || !n) return;
    switch (type) { case PFT_F64: memcpy(b, &v, 8); break; case PFT_U8: b[0] = (uint8_t)(int)v; break;
        case PFT_S16: { int16_t s = (int16_t)(int)v; memcpy(b, &s, 2); } break; case PFT_S32: { int32_t s = (int32_t)v; memcpy(b, &s, 4); } break; }
    int k = 0; while (k < pf_ned && pf_ed[k].off != off) k++;
    if (k == pf_ned) { if (pf_ned >= PF_NED) { snprintf(cam_msg, sizeof cam_msg, "record field 0x%x not edited: the dialog holds %d edits", off, PF_NED); return; } pf_ned++; }
    pf_ed[k].off = (uint16_t)off; pf_ed[k].n = (uint8_t)n; memcpy(pf_ed[k].v, b, n);
    CamParams *p = &pending.p; int t = p->type;
    if (t == CAM_POCKET && p->pk_ok && off >= 0x1228 && off + n <= 0x1228 + (int)sizeof p->pk_p) memcpy(p->pk_p + (off - 0x1228), b, n);
    if ((t == CAM_SLOT || t == CAM_HELIX || t == CAM_CIRCLE || t == CAM_THREAD || t == CAM_FACE) && p->mc_ok && off >= 0x1228 && off + n <= 0x1228 + (int)sizeof p->mc_p)
        memcpy(p->mc_p + (off - 0x1228), b, n);
    if (t == CAM_CIRCLE && off == 0x1238 && n == 1) p->mc_tip = b[0];   /* circle tip comp: mc_tip mirrors 0x1238 (cam_fileop.c) */
    if (t == CAM_FACE) cam_face_mirror(p, off, n, b);   /* batch 7: CamParams' face fields follow the record (cam_fileop.c) */
    if (t == CAM_POCKET && off == 0x892 && n == 1) p->mc_isl_depths = b[0];   /* audit R-U3: the refusal of Use island depths follows the page (cam2d_pock_rem.c) */
    if (t == CAM_CONTOUR) cam_tab_mirror(p, off, n, b);   /* Tabs 2834: the generator's tab fields follow the record */
}
static int pf_has_patch(int off) { for (int k = 0; k < pf_ned; k++) if (pf_ed[k].off == off) return 1; for (int k = 0; k < pending.p.nrpatch; k++) if (pending.p.rpatch[k].off == off) return 1; return 0; }   /* this dialog's edits or the op's record patches */
/* the contour direction kept in record 0x5d2 (op +0x56a): written only when it differs from the record's (or a patch already holds it) */
static void cd_dir_put(CamParams *p, int right) {
    if (pf_has_patch(0x5d2) || (int)pf_rv(0x5d2, PFT_U8) != right) pf_put(0x5d2, PFT_U8, right);
    p->ct_off_right = right;
}
/* S4 (cui_chain.c, msurf_assoc's flow): a record field of the dialog's op read / written as the pages do (T: 1 f64, 2 u8, 3 s16, 4 s32) */
double pf_field_get(int off, int t) { return pf_rv(off, t); }
void pf_field_set(int off, int t, double v) { pf_put(off, t, v); }
/* OK (generate_pending): the edited fields as record patches (cui_toolpage.c tp_rp: not heights) */
void pf_apply(CamParams *p) {
    /* Pocket Remachining 3146: MapToOp FUN_1801829b0 (mcmill_all.c l.268954-268965) stores what MapToDlg FUN_1801828b0 derived: the clearance 0x13c8 = 0x13c0 % x 0.01 x D (l.268928) and the roughing
       diameter 0x13b8 = 25 (metric) / 1 (inch) when the stored one is < nc_tol (l.268932-268939). The engine reads the records (FUN_1801ec3c0 l.338589) */
    if (pending.p.type == CAM_POCKET && pending.p.pk_ok && (int)pf_rv(0x13fa, PFT_S16) == 3) {
        double clr = pf_rv(0x13c0, PFT_F64) * 0.01 * pending.p.tool_dia, rd = pf_rv(0x13b8, PFT_F64);
        if (rd < mc_systol_units(mcam_is_metric(&F))->nc_tol) { rd = mcam_is_metric(&F) ? 25.0 : 1.0; pf_put(0x13b8, PFT_F64, rd); }
        pf_put(0x13c8, PFT_F64, clr);
        if (p->pk_ok) { memcpy(p->pk_p + (0x13c8 - 0x1228), &clr, 8); memcpy(p->pk_p + (0x13b8 - 0x1228), &rd, 8); }
    }
    /* Contour Remachining 2999 (decision N9-8): MapToOp FUN_18015bda0 stores the clearance MapToDlg FUN_18015bc90 derived: 0x1260 = 0x1258 % x 0.01 x D (decomp/mcmill_all.c l.243346) */
    if (pending.p.type == CAM_CONTOUR && pending.p.contour_type == 0 && pending.p.ct_rtype == 5) {
        pf_put(0x1260, PFT_F64, pf_rv(0x1258, PFT_F64) * 0.01 * pending.p.tool_dia);
        /* decision N9-13: FUN_18015bda0 stores also the tolerance MapToDlg derived, 0x1278 = 0x1270 % x D x 0.01 (l.243362-243363), and the roughing diameter it showed: 25 (metric) / 1 (inch) when the stored one is < nc_tol (l.243349-243358) */
        pf_put(0x1278, PFT_F64, pf_rv(0x1270, PFT_F64) * pending.p.tool_dia * 0.01);
        { double rd = pf_rv(0x1250, PFT_F64); if (rd < mc_systol_units(mcam_is_metric(&F))->nc_tol) pf_put(0x1250, PFT_F64, mcam_is_metric(&F) ? 25.0 : 1.0); }
    }
    for (int k = 0; k < pf_ned; k++) tp_rp(p, pf_ed[k].off, pf_ed[k].v, pf_ed[k].n);
    /* batch 8: Helix Bore 2823 MapToOp FUN_18016e840 writes the feeds the page's MapToDlg FUN_18016e630 derived from their % of the op's
       feed rate (record 0x378): 0x1270 = 0x1268 x 0.01 x F (final depth), 0x12a8 = 0x12a0 x 0.01 x F (finish); the engine
       (cam2d_ops.c hb_rough / hb_finish) reads 0x1270 / 0x12a8. Patched only when they differ (a stored op written by Mastercam with
       the same feed: nothing). The spindle pair 0x1290 / 0x1298 (S word) is not written: greyed (no F / S opt-in) */
    if (p->type == CAM_HELIX && p->mc_ok) {
        static const uint16_t PCT[2] = { 0x1268, 0x12a0 }, ABS[2] = { 0x1270, 0x12a8 };
        for (int i = 0; i < 2; i++) {
            double pc, ab; memcpy(&pc, p->mc_p + (PCT[i] - 0x1228), 8); memcpy(&ab, p->mc_p + (ABS[i] - 0x1228), 8);
            double nv = pc * 0.01 * p->feed;
            if (fabs(nv - ab) > 1e-9 * (1 + fabs(nv))) { memcpy(p->mc_p + (ABS[i] - 0x1228), &nv, 8); tp_rp(p, ABS[i], &nv, 8); }
        }
    }
}

/* the record offsets each engine reads from its block (cam2d_ops.c mcb / mcd / mcs by engine, cam2d_pock.c pkb / pkd /
   pks): a control on one of them is honoured as stored, so it is live here */
static int pf_honoured(int off) {
    /* Pocket: tapered walls generate for every type but Remachining (N9-19: process_pocket's taper arguments, cam2d_pock_taper.c; the open pocket cutting method FUN_1801e6970 / FUN_1801e5de0, cam2d_pock_open.c, reads only the outer angle) */
    if (pending.p.type == CAM_POCKET && pending.p.pk_ok && (off == 0x1288 || off == 0x1290 || off == 0x1298) && (int)pf_rv(0x13fa, PFT_S16) != 3) return 1;
    /* Remachining 2999 (FUN_180053a70 / FUN_1800550c0 read them; cam2d_remach_gen.c) */
    /* Pocket Remachining 3146 (HSTPocketRemachPage, DDX FUN_1801826b0; FUN_1801ec3c0 reads them, cam2d_pock_rem.c) */
    if (pending.p.type == CAM_POCKET && pending.p.pk_ok && (int)pf_rv(0x13fa, PFT_S16) == 3 && (off == 0x13b2 || off == 0x13b8 || off == 0x13c0 || off == 0x13c8 || off == 0x13d0 || off == 0x13d1 || off == 0x1280)) return 1;
    /* Pocket keep tool down (2026-11-12, cam2d_pock.c pk_ktd_*): finish 0x126a (FUN_1801cd200 l.318461) and depth cuts 0x891 (wr_pock_toolpath
       l.316099-316154, FUN_1801ede50 l.339478); Remachining still refuses 0x126a (cam2d_pock_rem.c) */
    if (pending.p.type == CAM_POCKET && pending.p.pk_ok && (off == 0x891 || (off == 0x126a && (int)pf_rv(0x13fa, PFT_S16) != 3))) return 1;
    if (pending.p.type == CAM_CONTOUR && pending.p.ct_rtype == 5 && (off == 0x124a || off == 0x1250 || off == 0x1258 || off == 0x1260 || off == 0x1270 || off == 0x1278 || off == 0x1280)) return 1;
    /* Contour Tabs 2834 (formats/contour_100_plan.md 2): the automatic placement cam2d.c ct_auto_tabs reads (add_auto_tabs /
       FUN_180223720): Automatic 0x1381, by max distance 0x1440 / 0x1448, Tab all 0x13f0, the shape size 0x13e0 / 0x13e8; Full / Partial
       0x1398 and the feed plane for full tabs 0x13b9 (cam2d.c cut_path_full_tabs, the tab writer FUN_180056be0) */
    if (pending.p.type == CAM_CONTOUR && (off == 0x1381 || off == 0x1440 || off == 0x1448 || off == 0x13f0 || off == 0x13e0 || off == 0x13e8
                                          || off == 0x1398 || off == 0x13b9 || off == 0x13ba || off == 0x139a
                                          || off == 0x13f1)) return 1;   /* + square point / Start-Mid-End (ct_square_tabs), Overwrite edited tabs (gen_contour) */
    /* Contour: first / last depth cut only (0x928 / 0x970, gen_contour lead_skip), keep tool down multi passes / depth cuts (0x8c8 /
       0x891, cam2d.c kd_link_ok) */
    if (pending.p.type == CAM_CONTOUR && (off == 0x928 || off == 0x970 || off == 0x8c8 || off == 0x891 || off == 0x992 || off == 0x993)) return 1;   /* + plunge after first / retract before last move (cam2d.c pass) */
    /* Contour: entry / exit feed overrides (0x1358 / 0x1350, 0x1368 / 0x1360: lead_in / lead_out), multipass feed override (0x1464 / 0x1458:
       the finish passes' feed) */
    if (pending.p.type == CAM_CONTOUR && (off == 0x1358 || off == 0x1350 || off == 0x1368 || off == 0x1360 || off == 0x1464 || off == 0x1458)) return 1;
    /* Contour: multipass spindle override (0x1465 / 0x1460): decoded to write nothing (cam2d.c gen_contour, 2026-11-07) - stored and shown */
    if (pending.p.type == CAM_CONTOUR && (off == 0x1465 || off == 0x1460)) return 1;
    /* batch 10: the Line/Arc filter block of the Arc Filter / Tolerance page 10318 (cam_filter.c reads it from the op's record with the
       page's patches: FUN_18004dd30 -> the filter globals; bnci_filter_op skips drill ops); XZ / YZ only with MCAM_ARC_FILTER_PLANES=1 */
    if (pending.p.type != CAM_DRILL && (off == 0x33a || off == 0x320 || off == 0x33b || off == 0x33e || off == 0x328 || off == 0x330
        || off == 0x33f || off == 0x340 || ((off == 0x33c || off == 0x33d) && cam_arc_filter_planes_on()))) return 1;
    /* 2026-11-08: the Smoothing block of page 10318 (cam_smooth.c reads it from the op's record with the page's patches: FUN_18094adc0 ->
       PointDistribution_3D FUN_18093bcf0 with record 0xc98 / 0xca9 / 0xcaa / 0xcab / 0xcac / 0xca0, on = 0xca8) - Contour only */
    if (pending.p.type == CAM_CONTOUR && (off == 0xca8 || off == 0xc98 || off == 0xca0 || (off >= 0xca9 && off <= 0xcac))) return 1;
    static const uint16_t POCK[] = { 0x1380, 0x1388, 0x1390, 0x1398, 0x13a0, /* Island facing panel 3002 (cam2d_pock_isl.c) */ 0x1230, 0x1258, 0x12a0, 0x12d0, 0x12d1, 0x1436, 0x12b8, 0x12c0, 0x12c8, 0x12f8, 0x1300, 0x1400, 0x123c, 0x12a2, 0x1408,
                                     0x12a8, 0x12b0, /* batch 6: the helix radii's distance sides (pairs, pf_set RPAIR) */
        /* batch 6 (formats/opdlg_port_audit.md 10): direction 0x123a (True Spiral FUN_1801ef3d0, finish FUN_1801cc170 l.317946),
           roughing angle 0x1250 (bound), Minimize tool burial 0x1259 (methods 2 / 3: the medial-axis links NOT DECODED -> FUN_1801e9ee0's
           fallback link + a warning, decisions batch dec1 B6-1; method 0: FUN_1801f0540 NOT DECODED, refused), entry (cam2d_pock_entry.c: helix options 0x12d2 / 0x12d3 / 0x12d8 / 0x12e8 / 0x12f0, entry feed 0x1308,
           ramp 0x1310-0x1370), finish (cam2d_pock_fin.c: 0x1268 / 0x1269 / 0x126b / 0x1436 / 0x1438 / 0x143a, By depth
           0x13f8) */
                                     0x123a, 0x1259, 0x12d2, 0x12d3, 0x12d8, 0x12e8, 0x12f0, 0x1308, 0x1310, 0x1318, 0x1320, 0x1328,
                                     0x1330, 0x1338, 0x1340, 0x1348, 0x1349, 0x134a, 0x1350, 0x1358, 0x1359, 0x1360, 0x1368, 0x1370,
                                     0x1268, 0x1269, 0x126b, 0x1438, 0x143a, 0x13f8,
                                     0x13fa, /* pocket type: all five generate */
                                     0x13e0, 0x13e8, 0x13f0, 0x13d8, /* panel 3001 Open pocket (HSTPocketOpenPage, cam2d_pock_open.c) */ 0 };
    static const uint16_t SLOT[] = { 0x1261, 0x1262, 0x1248, 0x1250, 0x1298, 0x1290, 0x1280, 0x1288, 0 };   /* gen_slot / slot_chain; batch 7:
                                     3020 1438 keep tool down 0x12b1 stays greyed - bnci_slotmill reads Depth Cuts' 0x891
                                     (FUN_180218e00), never 0x12b1; decisions batch dec1 (B7-3): 1847 / 1724 Output helixes as
                                     arcs / Tolerance 0x1280 / 0x1288 live (cam2d_slot_arc.c, FUN_1802189e0) */
    static const uint16_t FACE[] = { 0x1228, 0x122a, 0x1230, 0x1238, 0x1240, 0x1248, 0x1250, 0x1258, 0x1260, 0x1268, 0x1270, 0x1278,
                                     0x1280, 0x1288, 0x1290, 0x1298, 0x12a0, 0x12c0, 0x12c1, 0x12c2, 0 };   /* batch 7: bnci_face
                                     (cam2d_face.c); the distances of the pairs are the dialog's (the engine recomputes them) */
    static const uint16_t HELX[] = { 0x1248, 0x1249, 0x12b0, 0x12c0, 0x12c1, 0x1228, 0x1230, 0x1238, 0x1240, 0x1260, 0x1270, 0x1280,
                                     0x12a8, 0x12b8, 0x1258, 0 };                                                    /* gen_helixbore, hb_* */
    static const uint16_t CIRC[] = { 0x1238, 0x1260, 0x1261, 0x1262, 0x1278, 0x12a9, 0x12b9, 0x12e8, 0x12e9, 0x12ea, 0x1338, 0x1240,
                                     0x1248, 0x1250, 0x1258, 0x1268, 0x1290, 0x1298, 0x12a0, 0x12c0, 0x12d0, 0x12d8, 0x1330, 0 };   /* gen_circlemill, cm_* */
    static const uint16_t THRD[] = { 0x1280, 0x1281, 0x1282, 0x12a1, 0x12a3, 0x12c4, 0x1260, 0x1268, 0x1270, 0x1278, 0x1288, 0x1298,
                                     0x12a8, 0x12b8, 0x1228, 0x12b0, 0 };                                             /* gen_thread, tm_* */
    /* batch 9: Dynamic Mill - the fields mcce_parm_from_record (= LoadDynPockParm FUN_1801af380, decomp/mcmill_hst2d_dyn.c
       1497-1780; op + X = record X + 0x68) reads and the ported engine (mcce_dyn.c) uses; the generator reads the record with the
       dialog's patches (cam2d_dyn.c hst_record): approach 0x1660 (P+0x50, styles 0 / 2 / 3), start location 0x1790 (P+0x0c,
       FUN_180076500), first pass offset 0x17c0, first pass feed reduction 0x17b0, conventional feed 0x17d0 (with Zigzag:
       flags 4), cut order 0x16bf (P+0x20, the cut-order filters), retract combo 0x16b9 (flags 0x10000..0x80000, the link
       retract decision FUN_180064180); Entry Motion 3219: helix radius 0x16c0 (FUN_180078840), Z clearance 0x1688, plunge
       angle / entry pitch radio 0x16bd (flag 0x2000) and the pitch 0x1680; medax_de: trochoidal loop radius 0x16c8 (P+0x98, entry
       method 2, mcce_medent.c) and additional slot width 0x1690 (P+0xa0, entry method 4) */
    static const uint16_t DYN[] = { 0x1660, 0x1790, 0x17c0, 0x17b0, 0x17d0, 0x16bf, 0x16b9, 0x16c0, 0x1688, 0x16bd, 0x1680, 0x16c8, 0x1690, 0 };
    /* batch 9: Area Mill, Area Clearance (cam2d_area.c gen_area_hmm): the McHmm macro values it takes (cam_fileop.c
       cam_area_macro_params, mckernel FUN_18006b750 / FUN_18006af68, formats/area_mill.md 2-3) - MinOffset 0x1590 / MaxOffset
       0x1598, ProfileSmoothing 0x1566 with ProfileSmoothingCornerOffsetTolerance 0x15b0, LinkClimbMilling = (0x1564 == 0),
       MinProfileRampDiameter 0x15f0 (Transitions 1007) */
    static const uint16_t AREA[] = { 0x1564, 0x1590, 0x1598, 0x1566, 0x15b0, 0x15f0, 0 };
    /* batch 9 / HST engines: Dynamic Contour is generated (cam2d_dyn.c gen_dynamic; it ran only with MCCE_DCTOUR=1 before): the fields it reads are live -
       approach 0x1660, start location 0x1790, first pass offset 0x17c0, cut order 0x16bf, retract combo 0x16b9 and the Contour Wall
       page 3299 (HSTCtourWallPage MapToOp l.244493): stock thickness 0x1748 / radius of the shaping tool 0x1750 / its min toolpath
       radius 0x1758 = CDynPockParm +0xe8 / +0xf0 / +0xf8, the band of prep_ctour (FUN_180092950) */
    static const uint16_t DCT[] = { 0x1660, 0x1790, 0x17c0, 0x16bf, 0x16b9, 0x1748, 0x1750, 0x1758, 0 };
    /* HST engines: Peel Mill (cam2d_dyn.c gen_peel_mcce = CHST2DProcess::PreparePockBoundsPeel @1802b4090, decomp/hst2/
       peel_mcmill_spec.txt): 2784 slot width 0x1258 / side radios 0x1270 (one chain, l.6630-6649), Extend entry 0x1240 / 0x1278
       and exit 0x1272 / 0x1280 (l.6859-6868), approach 0x1660 (Motion entry extension, l.7709-7724), cutting style 0x17d8 (1 =
       "Peel": bnci_peel, refused), conventional feed 0x17d0 (LoadDynPockParm, with Zigzag) */
    static const uint16_t PEEL[] = { 0x1258, 0x1270, 0x1240, 0x1278, 0x1272, 0x1280, 0x1660, 0x17d8, 0x17d0, 0 };
    /* 3drough: Dynamic OptiRough (cam3d_rough.c c3_values / gen_optirough read these from the record with the dialog's patches):
       stepdown 0x1228, stepover 0x1230, min toolpath radius 0x1480, micro lift 0x1488, back feed 0x1490, conventional feed 0x1508,
       retract combo 0x16eb, gap radio / distance / % 0x16cd / 0x1308 / 0x1310; Transitions 1430: entry method 0x16e8 (Helix only;
       the medial ones are refused), helix radius 0x1328, loop radius 0x1450, slot width 0x1458, Z clearance 0x1330, plunge angle /
       entry pitch radio 0x16e9 with 0x1338 / 0x1460 */
    static const uint16_t OPTI[] = { 0x1228, 0x1230, 0x1480, 0x1488, 0x1490, 0x1508, 0x16eb, 0x16cd, 0x1308, 0x1310, 0x16e8, 0x1328, 0x1450,
                                     0x1458, 0x1330, 0x16e9, 0x1338, 0x1460, 0 };
    /* srough: Surface Rough Pocket (cam3d_srough.c gen_srough_pocket reads these from the record with the dialog's patches): stock 0x2e0,
       tolerance 0x1240, max stepdown 0x1248, containment offset 0x12b8 / 0x12c0 / 0x12c8, the refusal fields 0x1478 / 0x1420 / 0x161a /
       0x1434 / 0x1656 / 0x1640, of flat 0x15ca; the pocket block 0x1448.. goes to the 2D Pocket engine as its 0x1228.. (- 0x220): rough
       0x1458, finish 0x1459, climb 0x145a, method 0x145c, stepover 0x1468, angle 0x1470, burial 0x1479, passes 0x147a, spacing 0x1480,
       finish containment 0x1488, entry 0x14c0 */
    static const uint16_t SRPK[] = { 0x2e0, 0x1240, 0x1248, 0x12b8, 0x12c0, 0x12c8, 0x1478, 0x1420, 0x161a, 0x1434, 0x1656, 0x1640, 0x15ca,
                                     0x1458, 0x1459, 0x145a, 0x145c, 0x1468, 0x1470, 0x1479, 0x147a, 0x1480, 0x1488, 0x14c0, 0 };
    /* srough: Surface Rough Parallel (gen_srough_parallel): stock 0x2e0, tolerance 0x1240, max stepdown 0x1248, max stepover 0x13d8, angle
       0x13e0, cutting method 0x13e8, plunge control 0x13d2, negative / positive Z 0x13d0 / 0x13d1, the refused approximate start point 0x13ea */
    static const uint16_t SRPL[] = { 0x2e0, 0x1240, 0x1248, 0x13d8, 0x13e0, 0x13e8, 0x13d2, 0x13d0, 0x13d1, 0x13ea, 0x1239, 0 };
    /* Waterline (cam3d_finish.c gen_waterline): stepdown 0x1228, cut method 0x16bd / 0x16be, the refusal fields Add cuts 0x16c2, Corner rounding
       0x16c3, Bottom to top 0x16c1; tolerance 0x12a0 */
    static const uint16_t RAST[] = { 0x1230, 0x12f0, 0x16bd, 0x16d5, 0x16d4, 0x16cc, 0x16bf, 0x12a0, 0 };   /* Raster (cam3d_finish.c gen_raster) */
    static const uint16_t WATR[] = { 0x1228, 0x16bd, 0x16be, 0x16c2, 0x16c3, 0x16c0, 0x16c1, 0x12a0, 0 };
    /* Area Roughing (cam3d_finish.c gen_arearough): stepdown 0x1228, XY stepover Minimum / Maximum 0x12b8 / 0x12c0 (+ the % 0x1360 and 0x13e0 its
       handlers write), the refusal fields Add cuts 0x16c2 / Corner rounding 0x16c3, its offset tolerance 0x12c8, the cut method 0x16e4, tolerance 0x12a0 */
    /* Horizontal Area (cam3d_horiz.c gen_horizarea): stepover Minimum / Maximum 0x12b8 / 0x12c0 (+ 0x1360 / 0x13e0), the depth cuts 0x1558 / 0x13b8, the
       refused Corner rounding 0x16c3, the cut method 0x16e4, tolerance 0x12a0 */
    static const uint16_t HZAR[] = { 0x12b8, 0x12c0, 0x1360, 0x13e0, 0x1558, 0x13b8, 0x16c3, 0x16e4, 0x12a0, 0 };
    static const uint16_t AREAR[] = { 0x1228, 0x12b8, 0x12c0, 0x1360, 0x13e0, 0x16c2, 0x16c3, 0x12c8, 0x16e4, 0x12a0, 0 };
    /* Spiral / Radial (cam3d_finish.c gen_spiral / gen_radial): stepover 0x1230, the cut method 0x16bd / 0x16d5 / 0x16d4, the refused start point 0x16bf,
       center 0x13f0 / 0x13f8, inner / outer radius 0x1400 / 0x1408, Outer radius 0x16e3, clockwise 0x16df (Spiral), angles 0x1410 / 0x1418 (Radial) */
    static const uint16_t SPIR[] = { 0x1230, 0x16bd, 0x16d5, 0x16d4, 0x16bf, 0x13f0, 0x13f8, 0x1400, 0x1408, 0x16e3, 0x16df, 0x12a0, 0 };
    /* Scallop (gen_scallop): stepover 0x1230, the cut method bytes, the refused start point 0x16bf, Expand inside to out 0x16db, angles 0x1248 / 0x1250,
       Use Z depths 0x16c9 (refused), the collapse option 0x16e1 (refused) */
    static const uint16_t SCAL[] = { 0x1230, 0x16bd, 0x16d5, 0x16d4, 0x16bf, 0x16db, 0x1248, 0x1250, 0x16c9, 0x16e1, 0x12a0, 0 };
    /* Surface Finish Contour (gen_sfinish_contour): tolerance / stepdown 0x1240 / 0x1248, climb 0x13d4, open method 0x13d2, bottom to top 0x13e8, the
       refusal fields (shallow 0x1408, flats 0x1492, helix 0x1450, entry 0x1430, spiral 0x14b1, transition 0x12a0, start point 0x13d0, optimize 0x12a4),
       the short gap 0x1288 / 0x1290 / 0x1298 / 0x14a0 / 0x14a8, stock 0x2e0, depths 0x12e8 / 0x12e9 / 0x1300 / 0x1308 / 0x1310 / 0x1318 / 0x144e */
    static const uint16_t SFCT[] = { 0x1240, 0x1248, 0x13d4, 0x13d2, 0x13e8, 0x1408, 0x1492, 0x1450, 0x1430, 0x14b1, 0x12a0, 0x13d0, 0x12a4, 0x1288, 0x1290,
                                     0x1298, 0x14a0, 0x14a8, 0x2e0, 0x12e8, 0x12e9, 0x1300, 0x1308, 0x1310, 0x1318, 0x144e, 0 };
    /* HST Project (gen_hstproject): projection type 0x165a, source op 0x157c, depth cuts 0x1558 / 0x13b8, order by depth 0x165e, Raster's refusal bytes */
    static const uint16_t PROJ[] = { 0x165a, 0x157c, 0x1558, 0x13b8, 0x165e, 0x16bd, 0x16d5, 0x16d4, 0x16bf, 0x12a0, 0 };
    /* Rough Project (gen_srough_project): tolerance / stepdown, type 0x13d4, source 0x14e8, retract 0x1508, plunge 0x13d2, negz / posz 0x13d0 / 0x13d1, gap
       0x1288 / 0x1290 / 0x1298, stock 0x2e0, tip comp 0x1239, depths */
    static const uint16_t SRPJ[] = { 0x1240, 0x1248, 0x13d4, 0x14e8, 0x1508, 0x13d2, 0x13d0, 0x13d1, 0x1288, 0x1290, 0x1298, 0x2e0, 0x1239, 0x12e8, 0x1300,
                                     0x1308, 0x1310, 0x1318, 0 };
    /* Engrave (cam2d_engrave.c -> engrave.c engrave_setup): the Engrave block S = record 0x12f0 + the S offsets engrave_setup reads
       (direction 0x00, overlap 0x08, rough first / on / smooth 0x10 / 0x11 / 0x13, rough type / angle 0x14 / 0x18, on top 0x20, tolerance
       0x28, the height modes and values 0x30..0x74, sorting 0x9c, depth cuts / ramp 0xa0..0xb0, clearance at start / end 0xc0, roll 0xd0,
       flat cutter angle 0xd8, Enter On 0x130) and the ones it refuses on (remachining 0x90, XY stock 0xc8, wrapping 0xe4) */
    static const uint16_t ENGR[] = { 0x12f0, 0x12f8, 0x1300, 0x1301, 0x1303, 0x1304, 0x1308, 0x1310, 0x1318, 0x1320, 0x1324, 0x1328, 0x132c, 0x1330,
                                     0x1338, 0x1340, 0x1348, 0x1350, 0x1358, 0x1360, 0x1364, 0x1380, 0x138c, 0x1390, 0x1394, 0x1398, 0x139c, 0x13a0,
                                     0x13b0, 0x13b8, 0x13c0, 0x13c8, 0x13d4, 0x1420, 0 };
    /* Pencil (cam3d_pencil.c gen_pencil): the cut method bytes 0x16bd / 0x16d5 / 0x16d4, the refused start point 0x16bf and Use Z depths 0x16c9,
       the offsets 0x16cb / 0x1540 (refused unless none), Overthickness 0x12d0 (refused), Bitangency angle 0x12d8, tolerance 0x12a0 */
    static const uint16_t PENC[] = { 0x16bd, 0x16d5, 0x16d4, 0x16bf, 0x16c9, 0x16cb, 0x1540, 0x12d0, 0x12d8, 0x12a0, 0 };
    /* Blend (gen_blend -> cam3d_blend.c blend_params_from_record): tolerance 0x1240, stepover 0x1420, % 0x1438, method 0x1428, Along 0x142a, 2D
       0x142b, Quick 0x1440, stock 0x2e0, tip 0x1239, the short gap 0x1288 / 0x1290 / 0x1298 / 0x12a0 */
    static const uint16_t BLND[] = { 0x1240, 0x1420, 0x1438, 0x1428, 0x142a, 0x142b, 0x1440, 0x2e0, 0x1239, 0x1288, 0x1290, 0x1298, 0x12a0, 0 };
    /* Rough Plunge (gen_srough_plunge): stock 0x2e0, tolerance / stepdown / stepover 0x1240 / 0x1248 / 0x13d0, plunge path 0x141a, helix 0x13ec /
       0x13f0, tip comp 0x1239, top of stock 0x141c, depths 0x12e8 / 0x12e9 / 0x1300 / 0x1308 / 0x1310 / 0x1318 */
    static const uint16_t PLNG[] = { 0x2e0, 0x1240, 0x1248, 0x13d0, 0x141a, 0x13ec, 0x13f0, 0x1239, 0x141c, 0x12e8, 0x12e9, 0x1300, 0x1308, 0x1310, 0x1318, 0 };
    /* Surface Finish Radial (sr_parallel radial): tolerance, the rays 0x1300 / 0x1310 / 0x1318 / 0x1320, method 0x1308, start inside / outside
       0x130a, depth limits 0x12e8 / 0x12f0 / 0x12f8, stock 0x2e0, tip comp 0x1239, the short gap 0x1288 / 0x1290 / 0x1298 */
    static const uint16_t RADF[] = { 0x1240, 0x1300, 0x1308, 0x130a, 0x1310, 0x1318, 0x1320, 0x12e8, 0x12f0, 0x12f8, 0x2e0, 0x1239, 0x1288, 0x1290, 0x1298, 0 };
    /* Surface Finish Pencil (cam3d_pencil.c gen_sfpencil, page 240 CMsPnclPg): tolerance 0x1240, Machining direction 0x1320 / Climb 0x1304, start
       point 0x1305 (refused), Allow negative / positive Z 0x1300 / 0x1301, Parallel passes 0x1324 / number 0x1322 / stepover 0x1328 (refused unless
       None), Cut method 0x1326, Bitangency 0x1338, Overthickness 0x1340 (refused), depth limits 0x12e8 (refused), stock 0x2e0 */
    static const uint16_t SFPN[] = { 0x1240, 0x1320, 0x1304, 0x1305, 0x1300, 0x1301, 0x1324, 0x1322, 0x1328, 0x1326, 0x1338, 0x1340, 0x12e8, 0x12f0, 0x12f8, 0x2e0, 0 };
    static const uint16_t PRJF[] = { 0x1240, 0x1300, 0x1418, 0x1402, 0x1439, 0x12e8, 0x12f0, 0x12f8, 0x2e0, 0x1239, 0x1288, 0x1290, 0x1298, 0 };   /* Finish Project (gen_project_x finish) */
    static const uint16_t RADL[] = { 0x1230, 0x16bd, 0x16d5, 0x16d4, 0x16bf, 0x13f0, 0x13f8, 0x1400, 0x1408, 0x16e3, 0x1410, 0x1418, 0x12a0, 0 };
    /* batch 11: Misc Values 6555 - the op's misc integers / reals and the "set" flag (MiscellaneousValuesPageBase::MapToOpEnt
       18052e9f0): every op type; they reach the posted program only through Mastercam's NCI 1011 / 1012 (bnci_wr_1011 / 1012), i.e. the
       Mastercam post (.pst) path of a stored op (cam_postvals.c pv_nci_rewrite) and the saved part - the LinuxCNC post reads none */
    if (off == PV_MISC_SET || (off >= PV_MISC_INT && off < PV_MISC_INT + 40 && (off - PV_MISC_INT) % 4 == 0)
        || (off >= PV_MISC_REAL && off < PV_MISC_REAL + 80 && (off - PV_MISC_REAL) % 8 == 0)) return 1;
    const CamParams *p = &pending.p; const uint16_t *s = NULL;
    switch (p->type) { case CAM_POCKET: s = p->pk_ok ? POCK : NULL; break; case CAM_SLOT: s = SLOT; break; case CAM_HELIX: s = HELX; break;
        case CAM_FACE: s = FACE; break;
        case CAM_CIRCLE: s = CIRC; break; case CAM_THREAD: s = THRD; break;
        case CAM_DYNAMIC: s = DYN; break; case CAM_AREA: s = p->am_ok && p->am_mode == 1 ? AREA : NULL; break;
        case CAM_DYNCONTOUR: s = DCT; break; case CAM_PEEL: s = PEEL; break; case CAM_OPTIROUGH: s = OPTI; break; case CAM_SRPOCKET: case CAM_SRMPOCKET: s = SRPK; break; case CAM_SRPARALLEL: s = SRPL; break; case CAM_WATERLINE: s = WATR; break; case CAM_RASTER: s = RAST; break; case CAM_AREAROUGH: s = AREAR; break; case CAM_SPIRAL: s = SPIR; break; case CAM_RADIAL: s = RADL; break; case CAM_SCALLOP: s = SCAL; break; case CAM_PENCIL: s = PENC; break; case CAM_HORIZAREA: s = HZAR; break; case CAM_BLEND: s = BLND; break; case CAM_SRPLUNGE: s = PLNG; break; case CAM_SFRADIAL: s = RADF; break; case CAM_SFPENCIL: s = SFPN; break; case CAM_SFPROJECT: s = PRJF; break; case CAM_SFCONTOUR: s = SFCT; break; case CAM_HSTPROJECT: s = PROJ; break; case CAM_SRPROJECT: s = SRPJ; break; case CAM_ENGRAVE: s = ENGR; break; }
    if (p->type != CAM_POCKET && p->type != CAM_DYNAMIC && p->type != CAM_AREA && p->type != CAM_DYNCONTOUR && p->type != CAM_PEEL && p->type != CAM_OPTIROUGH && p->type != CAM_SRPOCKET && p->type != CAM_SRMPOCKET && p->type != CAM_SRPARALLEL && p->type != CAM_WATERLINE && p->type != CAM_RASTER && p->type != CAM_AREAROUGH && p->type != CAM_SPIRAL && p->type != CAM_RADIAL && p->type != CAM_SCALLOP && p->type != CAM_PENCIL && p->type != CAM_HORIZAREA && p->type != CAM_BLEND && p->type != CAM_SRPLUNGE && p->type != CAM_SFRADIAL && p->type != CAM_SFPROJECT && p->type != CAM_SFPENCIL && p->type != CAM_SFCONTOUR && p->type != CAM_HSTPROJECT && p->type != CAM_SRPROJECT && p->type != CAM_ENGRAVE && !p->mc_ok) s = NULL;
    for (; s && *s; s++) if (*s == off) return 1;
    return 0;
}
/* fields stored in radians that the page shows in degrees (formats/opdlg_fields_notes.md: lead sweep "display in deg
   inferred", circle high speed entry angle 0x1330 "shown in degrees", pocket helix plunge 0x12c0, ramp plunges
   0x1328 / 0x1330, taper 0x1290 / 0x1298 / 0x1310 / 0x1370 / 0x1738, tab ramp 0x13b0); 2826 1006 (0x15e0) is left
   as stored: its display unit is NOT DECODED */
static int pf_rad(const PfDef *d) {
    switch (d->dlg) {
    case 3013: case 2793: return d->id == 25174 || d->id == 25187;
    case 3271: return d->id == 1647;
    case 3010: return d->id == 1488;
    case 3011: return d->id == 1973 || d->id == 1542 || d->id == 1976;   /* batch 6: XY angle 0x1340 radians (FUN_1801c0e80, cam2d_pock_entry.c) */
    case 25598: case 3030: case 25602: return d->id == 1252 || d->id == 1466;
    case 2834: return d->id == 1213;
    case 2942: return d->id == 1758;   /* batch 7: roughing angle 0x1290, radians (bnci_face cos / sin; MCReal angle flag FUN_18016ba50) */
    case 1430: return d->id == 1443 && pending.p.type == CAM_OPTIROUGH;   /* 3drough: plunge angle 0x1338 radians (the 2 deg default = 0.0349) */
    case 1330: return d->id == 1284;                 /* Raster machining angle 0x12f0 (radians, mk l.88081-88087) */
    case 1339: return d->id == 679 || d->id == 681;  /* Radial start / end 0x1410 / 0x1418 (radians, mk l.88286-88297) */
    case 1328: return d->id == 1286;                 /* Scallop shallow angle 0x1388 */
    case 1036: return d->id == 905 || d->id == 807 || d->id == 901;   /* Finish Radial: angle increment / start / sweep (radians: the 360 deg default = 6.283) */
    case 1329: return d->id == 1300 || d->id == 1286;   /* Pencil Bitangency angle 0x12d8 (radians: the 170 deg default = 2.967), shallow angle 0x1388 */
    case 1342: return d->id == 1210 || d->id == 337; /* Scallop From / To 0x1248 / 0x1250 (radians: the 0 / 90 deg defaults = 0 / 1.5708) */
    }
    return 0;
}

/* ---- bound controls: the CamParams values the generators use, in the field's record convention */
#define K(D_, I_) ((long)(D_) * 100000L + (I_))
static double pf_clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static int pf_gap_mode(void) { return (int)pf_rv(0x15cd, PFT_U8) != 0; }
/* thread mill: the dialog's radial passes follow the multipass fields as params_from_record maps them (FUN_1802374d0),
   so the engine takes the record's fields (cam2d_ops.c gen_thread: thread_passes == mc_passes0) */
static void pf_thread_sync(CamParams *p) {
    int on = p->mc_mp_on != 0, sp = p->mc_ok ? (int)(int16_t)(p->mc_p[0x12b0 - 0x1228] | p->mc_p[0x12b1 - 0x1228] << 8) : 0;
    p->thread_passes = p->mc_passes0 = (on ? p->mc_mp_nr + p->mc_mp_nf : 1) + sp;
    p->thread_step = p->mc_step0 = on ? p->mc_mp_fs / 2 : 0;
}
static int pf_is_mc(int t) { return t == CAM_SLOT || t == CAM_HELIX || t == CAM_CIRCLE || t == CAM_THREAD; }
/* the Depth cuts group 3012: record 0x868 (mc_depth_on where params_from_record reads it; Facing: the record, its engine
   takes only the max rough step) or a max rough step (the engines' rule, cam2d.c hst_depths) */
static int pf_dc_on(void) { const CamParams *p = &pending.p; return (p->type == CAM_FACE ? (int)pf_rv(0x868, PFT_U8) : p->mc_depth_on) || p->stepdown > 1e-9; }
static int pf_lead_type(void) { int t = pending.p.type; return t == CAM_CONTOUR || t == CAM_POCKET; }
static int pf_lside(int out) { const CamParams *p = &pending.p; return p->lead_on && (out ? p->lead_out_on : p->lead_in_on); }
/* a side's sweep as the record holds it: a value from the record / a patch as is (0 = no arc on the contour, FUN_180051990
   bVar22 / bVar24), else this program's 0 = a quarter (cam2d.h lead_sw_rec; review 2026-10-23: a typed 0 was shown and
   written as 90) */
static double pf_lsw(int out) {
    const CamParams *p = &pending.p; double sw = out ? p->lead_xsw : p->lead_sw;
    return (p->lead_sw_rec & (out ? 2 : 1)) ? sw : sw > 0 ? sw : M_PI / 2;
}
/* a lead edit: the entry and exit sides' fields as record patches (both sides: an op whose patches hold the exit side has
   two leads when its sidecar is read again, cam_fileop.c cam_lead_sidecar_compat) - the values the engines take, % with
   their distances (B = A x D / 100) */
static void pf_lead_put(void) {
    const CamParams *p = &pending.p;
    pf_put(0x8f0, PFT_U8, p->lead_on); pf_put(0x8f8, PFT_U8, p->lead_in_on); pf_put(0x940, PFT_U8, p->lead_out_on);
    pf_put(0x900, PFT_F64, p->lead_len); pf_put(0x930, PFT_F64, p->lead_len_pct); pf_put(0x910, PFT_F64, p->lead_r); pf_put(0x938, PFT_F64, p->lead_r_pct);
    pf_put(0x918, PFT_F64, pf_lsw(0));
    pf_put(0x948, PFT_F64, p->lead_xlen); pf_put(0x978, PFT_F64, p->lead_xlen_pct); pf_put(0x958, PFT_F64, p->lead_xr); pf_put(0x980, PFT_F64, p->lead_xr_pct);
    pf_put(0x960, PFT_F64, pf_lsw(1));
    if (p->type == CAM_POCKET) { pf_put(0x8f9, PFT_U8, p->lead_in_perp); pf_put(0x941, PFT_U8, p->lead_out_perp); }   /* batch 6 */
    if (p->type == CAM_CONTOUR) {
        pf_put(0x8f9, PFT_U8, p->lead_in_perp); pf_put(0x941, PFT_U8, p->lead_out_perp);
        pf_put(0x908, PFT_F64, p->mc_lead_dz[0]); pf_put(0x920, PFT_F64, p->mc_lead_dz[1]); pf_put(0x950, PFT_F64, p->mc_lead_dz[2]); pf_put(0x968, PFT_F64, p->mc_lead_dz[3]);
    }
}
/* batch 5 helpers */
static int pf_cc_ok(void) { const CamParams *p = &pending.p; return nc_cutter_comp_on() && (p->contour_type == 0 || p->contour_type == 1); }   /* Control / Wear / Reverse wear offered
                                                    (2D and, since 2026-11-07, 2D chamfer: cam2d.c gen_contour, FUN_18005b340 l.1643-1655) */
static int pf_mp_on(void) { const CamParams *p = &pending.p; return p->finish_n > 0 || (p->ct_mp_rec && p->mc_mp_on); }   /* Multi Passes group 2945 */
/* Break Through 25599 (set_depths, cam_fileop.c cam_bt_eff): the flag / amount at the opcode's offsets; an edit moves the bottom
   by the change of what set_depths takes off (the record depth 0x1a0 stays, hf_apply adds pp_hf.bt back) */
static void pf_bt_update(void) {
    CamParams *p = &pending.p; int oc = pf_opcode(), bo = cam_bt_offset(oc); if (!bo) return;
    double nb = cam_bt_eff(oc, (int)pf_rv(bo, PFT_U8), pf_rv(bo + 8, PFT_F64), pp_hf.fstock);
    p->final_z -= nb - pp_hf.bt; pp_hf.bt = nb; p->bt_eff = nb;
}
/* batch 8: the drill page's post text (nc_post.c nc_drill_text: Generic Fanuc 3X Mill.pst page 0x27 entry cycle x 12 + k) */
static int pf_drill_pt(int k) { int on = 1; nc_drill_text(pending.p.drill_cycle, k, &on); return on; }
/* the cycles this program posts: 0-2, with MCAM_DRILL_CYCLES=1 also 3 Tap and 4 Bore #1 (nc_post.c) */
static int pf_drill_maxcyc(void) { return nc_drill_cycles_on() ? 4 : 2; }
/* Tip length 1339: HSTDrillTipCompPage::MapToDlg 0x1801655c0 / OnTipcompCb 0x180165a20 - tool_tip_length(D, tip angle) for a drill
   tool other than types 6-8 (Cnc::Tool::is_drill_tool), else nothing shown (bnci_drill_depth takes the breakthrough only) */
static double pf_drill_tip_len(void) {
    const CamParams *p = &pending.p; int tt = p->tool_type;
    int drill = !((unsigned)tt < 0x1a && ((0x3affc00u >> (tt & 0x1f)) & 1)) && (unsigned)(tt - 6) >= 3;
    return drill ? cam_tool_tip_length(p->tool_dia, p->tip_angle) : 0;
}
static int pf_rm_d(void) { return pending.p.tool_dia > 0 ? 1 : 0; }
static int pf_bget(const PfDef *d, double *v, int *live) {
    /* HSTContourRemachPage MapToDlg FUN_18015bc90 (decomp/mcmill_all.c): 1958 shows 25 (metric) / 1 (inch) when the stored value < nc_tol; 1716 = % x 0.01 x D, 2062 = % x D x 0.01 */
    /* HSTPocketRemachPage MapToDlg FUN_1801828b0 (decomp/mcmill_all.c l.268914): 1958 shows 25 (metric) / 1 (inch) when the stored value < nc_tol; 1716 = % x 0.01 x D */
    if (d->dlg == 3146 && pending.p.type == CAM_POCKET) switch (d->id) {
    case 1958: { double dv = pf_rv(0x13b8, PFT_F64); *v = dv < mc_systol_units(mcam_is_metric(&F))->nc_tol ? (mcam_is_metric(&F) ? 25.0 : 1.0) : dv; *live = (int)pf_rv(0x13b2, PFT_S16) == 2; return 1; }
    case 1716: *v = pf_rv(0x13c0, PFT_F64) * 0.01 * pending.p.tool_dia; *live = 1; return 1;
    }
    if (d->dlg == 2999 && pending.p.type == CAM_CONTOUR) switch (d->id) {
    case 1958: { double dv = pf_rv(0x1250, PFT_F64); *v = dv < mc_systol_units(mcam_is_metric(&F))->nc_tol ? (mcam_is_metric(&F) ? 25.0 : 1.0) : dv; *live = (int)pf_rv(0x124a, PFT_S16) == 2; return 1; }
    case 1716: *v = pf_rv(0x1258, PFT_F64) * 0.01 * pending.p.tool_dia; *live = 1; return 1;
    case 2062: *v = pf_rv(0x1270, PFT_F64) * pending.p.tool_dia * 0.01; *live = 1; return 1;
    }
    CamParams *p = &pending.p; int t = p->type; double D = p->tool_dia;
    *live = 1;
    switch (K(d->dlg, d->id)) {
    /* Stock to leave on walls 927 (record 0x2e0, written by mcam_saveops.c from stock_xy): live where the row-laid page had it */
    case K(2938, 927): case K(3025, 927): case K(2940, 927): case K(2942, 927): case K(2944, 927): case K(25592, 927): case K(3205, 927): case K(379, 927): case K(2784, 927):
        *v = p->stock_xy; *live = t != CAM_FACE && !(t == CAM_CONTOUR && p->contour_type); return 1;
    /* Stock to leave on floors 928 (record 0x888): the cut's bottom = depth + floor stock (mccore set_depths) */
    case K(2938, 928): case K(3025, 928): case K(2940, 928): case K(2944, 928): case K(25592, 928): case K(3205, 928): case K(379, 928): case K(2784, 928):
        *v = pp_hf.fstock; return 1;
    /* Contour 2938: Compensation type (this program: Computer / Off), direction, contour type, roll */
    /* 3025 = the Tab Cutoff op's Cut page (HSTCutoffCutPage, opcode 0x82): the same record fields (MapToDlg FUN_18015dab0 /
       UpdateLocalOp FUN_18015db80 read / write 0x5d0 / 0x5d2 / 0x5d3 / 0x1238 as the contour page); its functions enable / disable
       nothing - the greyed states below are this program's engine limits, as on 2938 */
    case K(2938, 1290): case K(3025, 1290): *v = p->side == SIDE_ON || p->contour_type == 2 ? 4 : p->ct_comp;
        *live = p->contour_type != 2 && (p->ct_comp == 0 || pf_cc_ok()); return 1;   /* a stored Control / Wear / Reverse wear without the switch: greyed */
    /* batch 5: Optimize cutter comp in control 2144 (0x5d4; FUN_180158ac0: comp = Control and type 2D / 2D chamfer / Ramp /
       Remachining), tip comp 1310 (0x1231: the taper's reference, FUN_1800569a0, and the NCI 1013 flag), corner rounding 1210 /
       break 1211 (0x1450 / 0x1468; FUN_180158ac0: disabled for Control comp and Remachining; cam2d.c ct_offset - 2D here) */
    case K(2938, 2144): *v = p->ct_ccopt; *live = p->side != SIDE_ON && (p->contour_type == 0 || p->contour_type == 1) && p->ct_comp == 1; return 1;
    /* Infinite look ahead 0x1238: enabled by FUN_180158ac0 (decomp/mcmill_all.c l.241430-241439): compensation in the computer, or Control with Optimize (2144) on, or Multi Passes on, or wall stock != 0, or type 2D / 3D chamfer. Cutoff page 3025: always. User decision D-5: closed chains only (cam2d.c chain_offset) */
    case K(2938, 1223): *v = !p->ct_nolook; *live = (p->side != SIDE_ON && p->ct_comp != 1) || (p->side != SIDE_ON && p->ct_comp == 1 && p->ct_ccopt) || pf_mp_on() || p->stock_xy != 0.0 || p->contour_type == 1 || p->ct_rtype == 2 || p->ct_rtype == 3; return 1;
    case K(3025, 1223): *v = !p->ct_nolook; return 1;
    case K(2938, 1310): case K(3025, 1310): *v = p->ct_tipcomp; *live = p->contour_type != 2; return 1;
    case K(2938, 1210): *v = p->ct_crnd; *live = p->side != SIDE_ON && p->contour_type == 0 && p->ct_comp != 1; return 1;
    case K(2938, 1211): *v = p->ct_cbrk; *live = p->side != SIDE_ON && p->contour_type == 0 && p->ct_comp != 1; return 1;
    case K(2938, 1274): case K(3025, 1274): if (p->side == SIDE_ON || p->contour_type == 2) { *v = pf_rv(0x5d2, PFT_U8); *live = 0; } else *v = p->side == 2; return 1;
    case K(2938, 2064): *v = p->contour_type == 1 ? 2 : p->contour_type == 0 && p->ct_rtype != 0 && p->ct_rtype != 2 ? p->ct_rtype : 0; *live = p->contour_type != 2; return 1;
    case K(2938, 1335): case K(3025, 1335): *v = p->roll; return 1;
    /* Ramp contour child 2997 (FUN_18015ae90; MapToDlg FUN_18015b220 / MapToOp FUN_18015b2c0 - no enable rule in the page's methods) */
    case K(2998, 597): case K(2998, 598): *v = p->ct_omode; return 1;   /* Oscillate child 2998 (HSTContourOscillatePage) */
    case K(2998, 599): *v = p->ct_odep; return 1;
    case K(2998, 600): *v = p->ct_odist; return 1;
    case K(2997, 2068): case K(2997, 2069): case K(2997, 2070): *v = p->ct_rmode; return 1;
    case K(2997, 1249): *v = p->ct_rang; return 1;
    case K(2997, 1255): *v = p->ct_rdep; return 1;
    case K(2997, 1842): *v = p->ct_rtol; return 1;
    case K(2997, 2067): *v = p->ct_r1way; return 1;
    case K(2997, 1450): *v = p->ct_rfinal; return 1;
    case K(2997, 1446): *v = p->ct_rlin; return 1;
    case K(2996, 1695): *v = p->chamfer_w; return 1;
    case K(2996, 1702): *v = p->chamfer_tip; return 1;
    case K(2940, 1335): *v = p->pk_ok ? p->pk_roll : 1; *live = p->pk_ok; return 1;
    case K(3002, 1284): *v = pf_rv(0x1398, PFT_F64); *live = (int)pf_rv(0x13fa, PFT_S16) == 2; return 1;   /* FUN_18017e260 l.266177: EnableWindow(0x504, flag +0x5da0: 1 = Island facing) */
    /* Face 2942 Style (batch 7): Mastercam's codes S16 0x1228 (FUN_18016ba50 MapToDlg: 0 Zigzag, 3 One pass, 5 Dynamic, any
       other code shows as One way = 4); the page's other fields are record fields (pf_honoured FACE, enables pf_en_rule) */
    case K(2942, 1855): { int st = p->mc_ok ? (int)pf_rv(0x1228, PFT_S16) : p->face_style; *v = st == 0 || st == 3 || st == 5 ? st : 4; return 1; }
    /* slot / circle / helix / thread: compensation type and direction (record 0x5d0 / 0x5d2, the engines' mc_comp) */
    case K(2944, 1290): case K(25592, 1290): case K(2824, 1665):
        if (!p->mc_ok) return 0;
        *v = p->mc_comp >= 0 && p->mc_comp <= 3 ? p->mc_comp : 4; *live = *v == 0 || *v == 4 || ((d->dlg == 2944 || d->dlg == 2824) && nc_cutter_comp_on()); return 1;   /* batch 7 (threadmill: Thread Mill too, cam2d_ops.c tm_pass):
                                                    Slot Mill takes Control / Wear / Reverse wear with MCAM_CUTTER_COMP=1 (cam2d_ops.c slot_chain) */
    case K(2944, 1274): case K(25592, 1274):
        if (!p->mc_ok) return 0;
        *v = p->mc_comp_right != 0; *live = p->mc_comp != 4; return 1;
    case K(25592, 1310): if (t != CAM_HELIX || !p->mc_ok) return 0; *v = p->mc_tip; return 1;   /* helix: p->mc_tip (gen_helixbore) */
    /* Drill 25595: cycle and 1st peck as the row-laid page (cycles 0-2 posted); Tip Comp 25594 group */
    case K(25595, 1056): *v = p->drill_cycle; *live = p->drill_cycle >= 0 && p->drill_cycle <= 19; return 1;   /* batch 8: a stored cycle
                                                    the post refuses can be changed to a posted one (the items: pf_bitems) */
    case K(25595, 1072): *v = p->peck; *live = (p->drill_cycle == 1 || p->drill_cycle == 2) && pf_drill_pt(7); return 1;
    /* batch 8: Dwell (record 0x1250, bnci_wr_81 +0x38): enabled by the post text (entry 3, nc_drill_text) and posted only with
       MCAM_DRILL_CYCLES=1 (G82 / G89 P), so live only with the switch and a cycle the post writes */
    case K(25595, 1089): *v = p->drill_dwell; *live = pf_drill_pt(3) && nc_drill_cycles_on() && (p->drill_cycle == 0 || p->drill_cycle == 4); return 1;
    case K(25594, 3019): *v = p->tip_comp; return 1;
    case K(25594, 1338): *v = p->drill_bt; *live = p->tip_comp != 0; return 1;   /* batch 8: buddy of 3019 (InitPage 0x180165440) */
    case K(25594, 1339): *v = pf_drill_tip_len(); *live = 0; return 1;       /* batch 8: tool_tip_length(D, tip angle) (MapToDlg 0x1801655c0) */
    case K(25594, 1202): *v = p->tool_dia; *live = 0; return 1;      /* the op's tool (mcam_saveops.c writes op_tool_info 0x348..) */
    case K(25594, 1204): *v = p->tip_angle; *live = 0; return 1;
    /* Thread 2824 */
    case K(2824, 1659): *v = p->thread_pitch; return 1;
    case K(2824, 1677): case K(2824, 1678): *v = p->thread_internal ? 0 : 1; return 1;
    case K(2824, 1690): case K(2824, 1691): *v = p->thread_rh ? 0 : 1; return 1;
    case K(2824, 1697): case K(2824, 1698): *v = p->thread_up ? 1 : 0; return 1;
    /* 2D High Speed 3205 / 2784 / 379 */
    case K(3205, 2078): case K(2784, 2078): *v = p->stepover_pct; return 1;
    /* batch 9: Area Cut 379 XY stepover (HSTCoreRoughPage, decomp/mcmill_all.c): % 2216 = record 0x1580, Min 2206 = 0x1590, Max 2207 =
       0x1598 (MapToDlg FUN_180188b20); live where Area Clearance runs (cam2d_area.c takes MinOffset / MaxOffset = 0x1590 / 0x1598) */
    case K(379, 2216): *v = p->stepover_pct; *live = p->am_ok && p->am_mode == 1; return 1;
    case K(379, 2206): case K(379, 2207): *v = pf_rv(d->id == 2206 ? 0x1590 : 0x1598, PFT_F64); *live = p->am_ok && p->am_mode == 1; return 1;
    case K(3205, 3199): case K(2784, 3199): *v = p->min_rad_pct; return 1;
    case K(3205, 3200): case K(2784, 3200): *v = p->micro_lift; return 1;
    case K(3205, 616): case K(2784, 616): *v = p->back_feed; return 1;
    case K(2784, 618): *v = p->slot_width; return 1;
    case K(3205, 3203): case K(3205, 3204): *v = pf_gap_mode(); return 1;
    case K(3205, 986): if (pf_gap_mode()) { *v = pf_rv(0x1600, PFT_F64); *live = 0; } else *v = p->link_retract; return 1;
    case K(3205, 3205): if (!pf_gap_mode()) { *v = pf_rv(0x1608, PFT_F64); *live = 0; } else *v = D > 0 ? p->link_retract * 100 / D : 0; return 1;
    /* (batch 9: 379's Keep tool down within 2346 / 2348 / 2350 / 2352 are no longer bound to the Dynamic gap: Area Clearance's links do
       not take LinkMaxStayOnSurface - greyed, pf_engine_nd) */
    /* Depth Cuts 25598 / 3030 / 25602 (batch 3): the group 3012 (record 0x868, DepthCutsPageBase; the engines' "on" =
       0x868 or a max rough step, cam2d.c hst_depths) with its buddies (UIGroupbox, DepthCutsPageBase::InitPage): max rough
       step (all), finish cuts / step where the engine takes them (contour gen_contour, pocket hst_depths, slot / circle / HST),
       keep tool down (slot / circle), tapered walls / angle and the depth cut order (contour: gen_contour) */
    case K(25598, 3012): case K(3030, 3012): case K(25602, 3012): *v = pf_dc_on(); return 1;
    case K(25598, 1228): case K(3030, 1228): case K(25602, 1228):
        *live = pf_dc_on(); *v = *live ? p->stepdown : pf_rv(0x870, PFT_F64); return 1;
    case K(25598, 1233): case K(25602, 1233): case K(3030, 1233):
        if (!(t == CAM_SLOT || t == CAM_CIRCLE || t == CAM_CONTOUR || t == CAM_POCKET || t == CAM_FACE || pp_hst(p))) return 0; *v = p->mc_depth_nf; *live = pf_dc_on(); return 1;
    case K(25598, 25195): case K(25602, 25195): case K(3030, 25195):   /* contour without finish cuts: the record's step (cam_fileop.c) */
        if (!(t == CAM_SLOT || t == CAM_CIRCLE || t == CAM_CONTOUR || t == CAM_POCKET || t == CAM_FACE || pp_hst(p))) return 0;
        *v = t == CAM_CONTOUR && p->mc_depth_nf <= 0 && p->mc_depth_fs == 0 ? pf_rv(0x880, PFT_F64) : p->mc_depth_fs; *live = pf_dc_on(); return 1;
    case K(25598, 1377): if (!(t == CAM_SLOT || t == CAM_CIRCLE || t == CAM_FACE) || !p->mc_ok) return 0; *v = p->mc_keep_down; *live = pf_dc_on(); return 1;   /* Facing
                                                    (batch 7): bnci_face local_4385 = depth cuts && Zigzag / One pass && 0x891 */
    case K(25598, 1381): if (t != CAM_CONTOUR) return 0; *v = p->dc_taper; *live = pf_dc_on() && p->contour_type != 2; return 1;   /* FUN_180056a70: opcode 1, any contour type (user 2026-10-23) */
    case K(25598, 1252): if (t != CAM_CONTOUR) return 0; *v = p->dc_taper_a; *live = pf_dc_on() && p->contour_type != 2 && p->dc_taper; return 1;   /* radians, shown in degrees */
    case K(25598, 1979): case K(25598, 1980): if (t != CAM_CONTOUR) return 0; *v = p->dc_bydepth; *live = pf_dc_on(); return 1;
    /* Lead In/Out 3013 (contour) / 2793 (pocket), batch 4: each side its own (cam_fileop.c LEADS; the engines: cam2d.c
       lead_sides / pass for the contour and the zigzag pocket's finish, cam2d_pock.c for the High Speed pocket). Enable
       rules: the side's controls with Lead In/Out (0x8f0) and that side on (FUN_18016fb00 / FUN_18016fcd0); the pocket takes
       the entry / exit on, lengths, radii and sweeps (its lead application FUN_1801cd200 is NOT DECODED: perpendicular,
       ramp / helix heights and overlap greyed there) */
    case K(3013, 3229): case K(2793, 3229): if (!pf_lead_type()) return 0; *v = p->lead_on; return 1;
    case K(3013, 3391): case K(2793, 3391): if (!pf_lead_type()) return 0; *v = p->lead_in_on; *live = p->lead_on; return 1;
    case K(3013, 3392): case K(2793, 3392): if (!pf_lead_type()) return 0; *v = p->lead_out_on; *live = p->lead_on; return 1;
    case K(3013, 25167): case K(3013, 1160): if (t != CAM_CONTOUR) return 0; *v = p->lead_in_perp; *live = pf_lside(0); return 1;
    /* batch 6: the pocket finish leads (cam2d_pock_fin.c, FUN_1801bdc00 / FUN_1801c2b30): perpendicular / tangent and the
       overlap 0x988 (FUN_1801bd7c0) */
    case K(2793, 25167): case K(2793, 1160): if (t != CAM_POCKET) return 0; *v = p->lead_in_perp; *live = pf_lside(0); return 1;
    case K(2793, 25182): case K(2793, 25188): if (t != CAM_POCKET) return 0; *v = p->lead_out_perp; *live = pf_lside(1); return 1;
    case K(2793, 25324): if (t != CAM_POCKET) return 0; *v = p->lead_ovl; *live = pf_lside(1); return 1;
    /* batch 6: Pocket Finishing 3004 cutter compensation (0x13a8; cam2d_pock_fin.c tags G41 / G42 / 140 as the contour of batch 5,
       posted only with MCAM_CUTTER_COMP=1) and Optimize (0x5d4, control only, FUN_1801cc170 l.318070) */
    case K(3004, 1814): if (t != CAM_POCKET || !p->pk_ok) return 0; *v = pf_rv(0x13a8, PFT_S16); *live = p->finish_n > 0 && nc_cutter_comp_on(); return 1;
    case K(3004, 2144): if (t != CAM_POCKET || !p->pk_ok) return 0; *v = p->ct_ccopt;
        *live = p->finish_n > 0 && nc_cutter_comp_on() && (int)pf_rv(0x13a8, PFT_S16) == 1; return 1;
    case K(3013, 25182): case K(3013, 25188): if (t != CAM_CONTOUR) return 0; *v = p->lead_out_perp; *live = pf_lside(1); return 1;
    case K(3013, 25166): case K(2793, 25166): if (!pf_lead_type()) return 0; *v = p->lead_len_pct; *live = pf_lside(0); return 1;
    case K(3013, 25165): case K(2793, 25165): if (!pf_lead_type()) return 0; *v = p->lead_len; *live = pf_lside(0); return 1;
    case K(3013, 25172): case K(2793, 25172): if (!pf_lead_type()) return 0; *v = p->lead_r_pct; *live = pf_lside(0); return 1;
    case K(3013, 25171): case K(2793, 25171): if (!pf_lead_type()) return 0; *v = p->lead_r; *live = pf_lside(0); return 1;
    case K(3013, 25174): case K(2793, 25174): if (!pf_lead_type()) return 0; *v = pf_lsw(0); *live = pf_lside(0); return 1;
    case K(3013, 25181): case K(2793, 25181): if (!pf_lead_type()) return 0; *v = p->lead_xlen_pct; *live = pf_lside(1); return 1;
    case K(3013, 25180): case K(2793, 25180): if (!pf_lead_type()) return 0; *v = p->lead_xlen; *live = pf_lside(1); return 1;
    case K(3013, 25186): case K(2793, 25186): if (!pf_lead_type()) return 0; *v = p->lead_xr_pct; *live = pf_lside(1); return 1;
    case K(3013, 25185): case K(2793, 25185): if (!pf_lead_type()) return 0; *v = p->lead_xr; *live = pf_lside(1); return 1;
    case K(3013, 25187): case K(2793, 25187): if (!pf_lead_type()) return 0; *v = pf_lsw(1); *live = pf_lside(1); return 1;
    case K(3013, 25369): if (t != CAM_CONTOUR) return 0; *v = p->mc_lead_dz[0]; *live = pf_lside(0); return 1;
    case K(3013, 25219): if (t != CAM_CONTOUR) return 0; *v = p->mc_lead_dz[1]; *live = pf_lside(0); return 1;
    case K(3013, 25370): if (t != CAM_CONTOUR) return 0; *v = p->mc_lead_dz[2]; *live = pf_lside(1); return 1;
    case K(3013, 25220): if (t != CAM_CONTOUR) return 0; *v = p->mc_lead_dz[3]; *live = pf_lside(1); return 1;
    case K(3013, 25324): if (t != CAM_CONTOUR) return 0; *v = p->lead_ovl; *live = p->lead_on; return 1;   /* FUN_180050a10 */
    case K(3013, 25158): if (t != CAM_CONTOUR) return 0; *v = p->lead_mid; *live = p->lead_on; return 1;
    case K(3013, 25200): if (t != CAM_CONTOUR) return 0; *v = p->lead_gouge; *live = p->lead_on; return 1;
    /* Use entry / exit point and Use point depth (0x929 / 0x92a, 0x971 / 0x972): the page's enable (decomp/mcmill_all.c l.256637-256650
       entry, l.256692-256705 exit) - the side on and the page member +0x11ad8, which OnSetActive (l.258128-258136) sets for a contour
       (opcode 1 / 0x85) to FUN_18016fea0 != 4: the contour type (+0x3390 = record 0x1308; 0..4 in 2D, 0..2 with ctour_3d, else 0) is
       not Ramp; "Use point depth" also needs its "Use point" checked (BM_GETCHECK on +0x2a80 / +0x2b..) */
    case K(3013, 25169): if (t != CAM_CONTOUR) return 0; *v = (p->lead_xflags & 2) != 0; *live = pf_lside(0) && p->ct_rtype != 4; return 1;
    case K(3013, 25170): if (t != CAM_CONTOUR) return 0; *v = (p->lead_xflags & 64) != 0; *live = pf_lside(0) && p->ct_rtype != 4 && (p->lead_xflags & 2); return 1;
    case K(3013, 25183): if (t != CAM_CONTOUR) return 0; *v = (p->lead_xflags & 8) != 0; *live = pf_lside(1) && p->ct_rtype != 4; return 1;
    case K(3013, 25184): if (t != CAM_CONTOUR) return 0; *v = (p->lead_xflags & 128) != 0; *live = pf_lside(1) && p->ct_rtype != 4 && (p->lead_xflags & 8); return 1;
    /* adjust start / end of contour (FUN_18015a760 / FUN_180159370 / FUN_1801592e0: with Lead In/Out on, the radios and lengths
       with their check box; FUN_180051990 uses them only with the leads on) */
    case K(3013, 1225): if (t != CAM_CONTOUR) return 0; *v = p->adj_s; *live = p->lead_on; return 1;
    case K(3013, 1226): if (t != CAM_CONTOUR) return 0; *v = p->adj_e; *live = p->lead_on; return 1;
    case K(3013, 1300): case K(3013, 1164): if (t != CAM_CONTOUR) return 0; *v = p->adj_s_ext; *live = p->lead_on && p->adj_s; return 1;
    case K(3013, 1304): case K(3013, 1167): if (t != CAM_CONTOUR) return 0; *v = p->adj_e_ext; *live = p->lead_on && p->adj_e; return 1;
    case K(3013, 1990): if (t != CAM_CONTOUR) return 0; *v = p->adj_s_pct; *live = p->lead_on && p->adj_s; return 1;
    case K(3013, 1302): if (t != CAM_CONTOUR) return 0; *v = p->adj_s_len; *live = p->lead_on && p->adj_s; return 1;
    case K(3013, 1992): if (t != CAM_CONTOUR) return 0; *v = p->adj_e_pct; *live = p->lead_on && p->adj_e; return 1;
    case K(3013, 1306): if (t != CAM_CONTOUR) return 0; *v = p->adj_e_len; *live = p->lead_on && p->adj_e; return 1;
    case K(3271, 3237): if (!p->mc_ok) return 0; *v = p->mc_lead_on; return 1;   /* circle: leads on (cm_circle p->mc_lead_on) */
    /* Thread Mill Linking 2837 (record 0x1230 clearance, 0x1238 feed plane, 0x1240 top, 0x1248 depth; 0x1250 = 1: the
       last three from the point's Z, cam_fileop.c / mcam_saveops.c) */
    case K(2837, 1650):   /* the op's 0x1230 (hf_from_record: a stored op's, a new op's from the defaults op - FUN_180236810 rapids to
                             it with no on / off switch); the Linking flags' rule only for an op without it */
        if (pp_hf.thr_clr_on) { *v = pp_hf.thr_clr; return 1; }
        *v = pp_hf.clr_on ? pp_hf.clr_abs : pp_hf.ret_on && p->retract_z > -1e29 ? p->retract_z : p->top_z + p->feed_plane; return 1;
    case K(2837, 1651): case K(2837, 1653): case K(2837, 1654): {
        double zc = (int)pf_rv(0x1250, PFT_S16) == 1 ? pp_hf.cz : 0;
        *v = d->id == 1653 ? p->top_z - zc : d->id == 1654 ? p->final_z - zc : p->top_z + p->feed_plane - zc; return 1; }
    case K(2837, 1059): *v = pp_hf.clr_end; return 1;
    /* batch 8: the page's Absolute / Incremental radio (S16 0x1250, MapToDlg FUN_1801939a0 / MapToOp FUN_180193a40): the feed plane,
       top and depth edits are written as typed, the engine adds the point's Z when incremental (cam_fileop.c CAM_THREAD) */
    case K(2837, 1655): case K(2837, 1656): *v = pf_rv(0x1250, PFT_S16); return 1;
    /* batch 8: Circle diameter (helix 0x1240 / circle 0x1258): HSTPtBasedCutPage MapToDlg 0x180185840 enables it only when a picked
       point is not an arc (the engines take the arc's own radius, cam2d_ops.c gen_helixbore / gen_circlemill) */
    case K(25592, 1735): { if (!p->mc_ok) return 0; int off = t == CAM_HELIX ? 0x1240 : 0x1258; *v = pf_rv(off, PFT_F64); *live = 0;
        for (int i = 0; i < pending.nholes; i++) if (!(pending.holes[i].d > 0)) *live = 1;
        return 1; }
    /* Multi Passes: contour 25591 (finish = finish_n / finish_step as the row-laid page; rough count / spacing / all
       depths from the record when the op's multipasses are on, cam2d.c gen_contour), thread 2825 (the record fields) */
    case K(25591, 2945): *v = pf_mp_on(); return 1;   /* batch 5: the group live (IsPageDisabled 0x1801772b0 / MapToOp: rec 0x8a8), its
                                                         buddies with it */
    case K(25591, 1271): *v = p->finish_n; *live = pf_mp_on(); return 1;
    case K(25591, 1275): *v = p->finish_step; *live = pf_mp_on() && p->finish_n > 0; return 1;
    case K(25591, 1264): *live = p->ct_mp_rec && p->mc_mp_on; *v = *live ? p->mc_mp_nr : 1; return 1;
    case K(25591, 1267): *live = p->ct_mp_rec && p->mc_mp_on; *v = *live ? p->mc_mp_rs : 0; return 1;
    case K(25591, 1260): case K(25591, 1262): *live = p->ct_mp_rec && p->mc_mp_on; *v = *live ? p->ct_fin_all : 0; return 1;
    case K(2825, 2945): case K(3270, 2945): if (!p->mc_ok) return 0; *v = p->mc_mp_on; return 1;
    case K(2825, 1264): case K(3270, 1264): if (!p->mc_ok) return 0; *v = p->mc_mp_nr; *live = p->mc_mp_on; return 1;
    case K(2825, 1267): case K(3270, 1267): if (!p->mc_ok) return 0; *v = p->mc_mp_rs; *live = p->mc_mp_on; return 1;
    case K(2825, 1271): case K(3270, 1271): if (!p->mc_ok) return 0; *v = p->mc_mp_nf; *live = p->mc_mp_on; return 1;
    case K(2825, 1275): case K(3270, 1275): if (!p->mc_ok) return 0; *v = p->mc_mp_fs; *live = p->mc_mp_on; return 1;
    /* Tabs 2834 */
    case K(3022, 3197): *v = p->tab_cut != 0; *live = p->tabs > 0; return 1;   /* MapToDlg FUN_180191570: the group = 0x13f2, enabled with 0x1380 */
    case K(3022, 1070): case K(3022, 1144): case K(3022, 1147): *v = p->tab_cut; *live = p->tabs > 0 && p->tab_cut != 0; return 1;
    case K(3022, 1150): *v = p->tab_cut_ffp; *live = p->tabs > 0 && p->tab_cut != 0 && (pf_tc_click ? pf_tc_click == 2 : p->tab_cut == 3); return 1;
    case K(2834, 1129): *v = p->tabs > 0; return 1;   /* batch 5: the group live (0x1380, saved from tabs > 0), its buddies with it */
    case K(2834, 1111): *v = p->tab_rec ? p->tab_n : p->tabs; *live = p->tabs > 0 && (!p->tab_rec || (pf_rv(0x1381, PFT_U8) && !pf_rv(0x1440, PFT_U8))); return 1;   /* FUN_180190d80 */
    case K(2834, 1085): *v = p->tab_w; *live = p->tabs > 0; return 1;
    case K(2834, 1088): *v = p->tab_h; *live = p->tabs > 0; return 1;
    case K(2834, 1208): case K(2834, 1201): *v = p->tab_ramp != 0; *live = p->tabs > 0; return 1;   /* 0x139c (cam2d.c cut_path) */
    case K(2834, 1213): *v = p->tab_ang; *live = p->tabs > 0 && p->tab_ramp; return 1;            /* 0x13b0 rad, shown in deg */
    /* Break Through 25599 (the opcode's flag / amount, cam_fileop.c cam_bt_offset) */
    case K(25599, 1220): { int bo = cam_bt_offset(pf_opcode()); if (!bo) return 0; *v = pf_rv(bo, PFT_U8); return 1; }
    case K(25599, 1338): { int bo = cam_bt_offset(pf_opcode()); if (!bo) return 0; *v = pf_rv(bo + 8, PFT_F64); *live = pf_rv(bo, PFT_U8) != 0; return 1; }
    /* Pocket Roughing 3003 / Finishing 3004 / Entry 3005 + 3010 */
    case K(3003, 25329): *v = p->stepover_pct; return 1;
    case K(3003, 1364): *v = p->stepover_pct * D / 100; return 1;   /* batch 6: the distance side of 25329 (B = A x D / 100) */
    case K(3003, 1360): *v = !p->pocket_no_rough; return 1;
    case K(3003, 1419): *v = p->pk_ok ? pf_rv(0x123c, PFT_S16) : 8; *live = p->pk_ok; return 1;
    case K(3003, 1366): *v = p->pocket_angle; return 1;
    case K(3004, 1361): *v = p->finish_n > 0; *live = 0;
        if (p->pk_ok && (int)pf_rv(0x13fa, PFT_S16) == 4 && pf_rv(0x13f0, PFT_U8) != 0) { *v = pf_rv(0x1239, PFT_U8) != 0; *live = 1; }   /* Open pocket cutting method: the flag 0x1239 itself (FUN_1801e7040 l.335064-335071, cam2d_pock_open.c) */
        return 1;
    case K(3004, 1369): *v = p->finish_n; return 1;
    case K(3004, 1370): *v = p->finish_step; *live = p->finish_n > 0; return 1;
    case K(3010, 1488): *v = p->pk_ok ? pf_rv(0x12c0, PFT_F64) : p->ramp_deg * M_PI / 180; return 1;   /* radians, shown in degrees */
    /* Slot 3020, Helix Bore 2823 */
    case K(3020, 2079): *v = p->stepover_pct; return 1;
    case K(3020, 2077): *v = p->stepover_pct * D / 100; return 1;   /* batch 7: the distance side (0x1270 = 0x1268 % x 0.01 x D, bnci_slotmill) */
    case K(3020, 2083): *v = p->ramp_deg; return 1;
    case K(2823, 1700): *v = p->helix_pitch; return 1;
    /* batch 8: the feed pairs (% 2157 = 0x1268 / feed 2158 = 0x1270, % 2159 = 0x12a0 / feed 2160 = 0x12a8): MapToDlg FUN_18016e630 shows
       the feed as % x 0.01 x the op's feed rate (record 0x378 = local op +0x2400) */
    case K(2823, 2157): case K(2823, 2159): if (!p->mc_ok) return 0; *v = pf_rv(d->id == 2157 ? 0x1268 : 0x12a0, PFT_F64); *live = d->id == 2157 || p->finish_n > 0; return 1;
    case K(2823, 2158): case K(2823, 2160): if (!p->mc_ok) return 0; *v = pf_rv(d->id == 2158 ? 0x1268 : 0x12a0, PFT_F64) * 0.01 * p->feed; *live = d->id == 2158 || p->finish_n > 0; return 1;
    case K(2823, 1439): *v = p->finish_n > 0; return 1;
    case K(2823, 1732): *v = p->finish_step; *live = p->finish_n > 0; return 1;
    /* Dynamic entry 3219: plunge angle (record 0x1678, degrees) */
    case K(3219, 3216): *v = p->ramp_deg; *live = pf_rv(0x16bd, PFT_U8) == 0; return 1;   /* batch 9: FUN_180169600 - radio 0 */
    /* batch 9: Entry method 3206 = record 0x16bb (MapToDlg FUN_180169e40 / UpdateLocalOp FUN_18016ae90); the items: pf_bitems */
    case K(3219, 3206): *v = pf_rv(0x16bb, PFT_U8); *live = p->type == CAM_DYNAMIC; return 1;
    }
    return 0;
}
static int pf_bset(const PfDef *d, double v) {
    if (d->dlg == 3146 && pending.p.type == CAM_POCKET) {   /* kill focus FUN_180182a60 / FUN_180182b20 (mcmill_all.c l.268971-269027): 1716 -> % = dist x 100 / D, 1984 -> dist = % x D x 0.01 */
        double D = pending.p.tool_dia;
        switch (d->id) {
        case 1984: pf_put(0x13c0, PFT_F64, v); pf_put(0x13c8, PFT_F64, v * 0.01 * D); return 1;
        case 1716: pf_put(0x13c8, PFT_F64, v); if (D > 0) pf_put(0x13c0, PFT_F64, v * 100.0 / D); return 1;
        }
    }
    if (d->dlg == 2999 && pending.p.type == CAM_CONTOUR) {   /* kill focus FUN_18015be50 / FUN_18015bf10 / FUN_18015bfd0 / FUN_18015c090 */
        double D = pending.p.tool_dia;
        switch (d->id) {
        case 1984: pf_put(0x1258, PFT_F64, v); pf_put(0x1260, PFT_F64, v * 0.01 * D); return 1;
        case 1716: pf_put(0x1260, PFT_F64, v); if (D > 0) pf_put(0x1258, PFT_F64, v * 100.0 / D); return 1;
        case 2061: pf_put(0x1270, PFT_F64, v); pf_put(0x1278, PFT_F64, v * D * 0.01); return 1;
        case 2062: pf_put(0x1278, PFT_F64, v); if (D > 0) pf_put(0x1270, PFT_F64, v * 100.0 / D); return 1;
        }
    }
    CamParams *p = &pending.p; int t = p->type; double D = p->tool_dia;
    switch (K(d->dlg, d->id)) {
    case K(2938, 927): case K(3025, 927): case K(2940, 927): case K(2942, 927): case K(2944, 927): case K(25592, 927): case K(3205, 927): case K(379, 927): case K(2784, 927):
        p->stock_xy = pf_clamp(v, -10, 10); return 1;
    case K(2938, 928): case K(3025, 928): case K(2940, 928): case K(2944, 928): case K(25592, 928): case K(3205, 928): case K(379, 928): case K(2784, 928):
        p->final_z += v - pp_hf.fstock; pp_hf.fstock = v; pf_put(0x888, PFT_F64, v); pf_bt_update(); return 1;   /* the depth (0x1a0) stays; set_depths'
                                                                                                         break through follows the floor stock */
    case K(2938, 1290): case K(3025, 1290): { int off = v == 4, ns = off ? SIDE_ON : p->side == SIDE_ON ? ((int)pf_rv(0x5d2, PFT_U8) ? 2 : 1) : p->side == 2 ? 2 : 1;   /* back from Off: op +0x56a's direction */
        /* Off keeps the last direction set: UpdateLocalOp FUN_180157e30 (decomp/mcmill_all.c l.240808-240811) stores both combos - type to op +0x568,
           direction to op +0x56a (local op +0x2658 / +0x265a) - whatever the type; under Off the direction combo is only greyed. Here the side is lost
           with SIDE_ON, so the direction goes to record 0x5d2 (mcam_saveops.c keeps 0x5d2 under Off) when it differs from the record's */
        if (off && p->side != SIDE_ON && p->side_rel) cd_dir_put(p, p->side == 2);
        if (ns != p->side) { p->side = ns; p->side_rel = 1; }
        int c = off ? p->ct_comp : (int)pf_clamp(v, 0, 3);
        if (c != p->ct_comp || (!off && c)) { if (c || p->ct_comp) pf_put(0x5d0, PFT_S16, c); p->ct_comp = c; }   /* 0x5d0 patched only around Control /
                                                                    Wear / Reverse wear (Computer / Off: mcam_saveops.c from side) */
        return 1; }
    case K(2938, 2144): p->ct_ccopt = v != 0; pf_put(0x5d4, PFT_U8, v != 0); return 1;
    case K(2938, 1223): case K(3025, 1223): p->ct_nolook = v == 0; pf_put(0x1238, PFT_U8, v != 0); return 1;
    case K(2938, 1310): case K(3025, 1310): p->ct_tipcomp = v != 0; pf_put(0x1231, PFT_U8, v != 0); return 1;
    case K(2938, 1210): p->ct_crnd = pf_clamp(v, 0, 1000); pf_put(0x1450, PFT_F64, p->ct_crnd); return 1;
    case K(2938, 1211): p->ct_cbrk = pf_clamp(v, 0, 1000); pf_put(0x1468, PFT_F64, p->ct_cbrk); return 1;
    case K(25591, 2945): p->mc_mp_on = v != 0; p->ct_mp_rec = 1; pf_put(0x8a8, PFT_U8, v != 0);
        if (v) { p->mc_mp_nr = (int)pf_rv(0x8aa, PFT_S16); p->mc_mp_rs = pf_rv(0x8b0, PFT_F64); p->ct_fin_all = (int)pf_rv(0x1230, PFT_U8); }
        else p->finish_n = 0;
        pp_mp_pic = v != 0; return 1;   /* OnBnClickedMultipassCb: the picture follows the group */
    case K(3022, 3197): {   /* UpdateLocalOp FUN_180191660: off -> 0, on -> the selected radio's index + 1 */
        if (p->tab_cut) pf_tc_last = p->tab_cut;
        p->tab_cut = v ? pf_tc_last : 0; pf_put(0x13f2, PFT_S16, p->tab_cut); return 1; }
    case K(3022, 1070): case K(3022, 1144): case K(3022, 1147):
        p->tab_cut = (int)pf_clamp(v, 1, 3); pf_tc_last = p->tab_cut; pf_tc_click = p->tab_cut; pf_put(0x13f2, PFT_S16, p->tab_cut); return 1;
    case K(3022, 1150): p->tab_cut_ffp = v != 0; pf_put(0x13f4, PFT_U8, v != 0); return 1;
    case K(2834, 1129): p->tabs = v ? (p->ntab_xy > 0 ? p->ntab_xy : (int)pf_clamp(pf_rv(0x1382, PFT_S16), 1, 12)) : 0; return 1;   /* count 0x1382 (MM / Inch 4) */
    case K(2834, 1208): case K(2834, 1201): p->tab_ramp = v != 0; pf_put(0x139c, PFT_S16, v != 0); return 1;
    case K(2834, 1213): p->tab_ang = pf_clamp(v, 0.01 * M_PI / 180, 89.99 * M_PI / 180); pf_put(0x13b0, PFT_F64, p->tab_ang); return 1;
    case K(25599, 1220): { int bo = cam_bt_offset(pf_opcode()); if (!bo) return 0; pf_put(bo, PFT_U8, v != 0); pf_bt_update(); return 1; }
    case K(25599, 1338): { int bo = cam_bt_offset(pf_opcode()); if (!bo) return 0; pf_put(bo + 8, PFT_F64, pf_clamp(v, 0, 1e6)); pf_bt_update(); return 1; }
    case K(2938, 1274): case K(3025, 1274): { int ns = v ? 2 : 1; if (ns != p->side) { p->side = ns; p->side_rel = 1; if (pf_has_patch(0x5d2)) cd_dir_put(p, ns == 2); } return 1; }   /* a 0x5d2 patch (Off above) follows the direction (UpdateLocalOp l.240810-240811) */
    case K(2938, 2064): p->contour_type = v == 2 ? 1 : 0; p->ct_rtype = v == 2 ? 2 : v == 4 ? 4 : v == 5 ? 5 : v == 6 ? 6 : 0; return 1;   /* + Ramp / Remachining / Oscillate */
    case K(2938, 1335): case K(3025, 1335): p->roll = (int)pf_clamp(v, 0, 2); return 1;
    case K(2998, 597): case K(2998, 598): p->ct_omode = (int)pf_clamp(v, 0, 1); pf_put(0x1408, PFT_S16, p->ct_omode); return 1;
    case K(2998, 599): p->ct_odep = pf_clamp(v, -1e6, 1e6); pf_put(0x1418, PFT_F64, p->ct_odep); return 1;
    case K(2998, 600): p->ct_odist = pf_clamp(v, -1e6, 1e6); pf_put(0x1410, PFT_F64, p->ct_odist); return 1;
    case K(2997, 2068): case K(2997, 2069): case K(2997, 2070): p->ct_rmode = (int)pf_clamp(v, 0, 2); pf_put(0x12c0, PFT_S16, p->ct_rmode); return 1;
    case K(2997, 1249): p->ct_rang = pf_clamp(v, 0, 90); pf_put(0x12c8, PFT_F64, p->ct_rang); return 1;   /* the edit's minimum 0 (+0x2f60, FUN_18015b220) */
    case K(2997, 1255): p->ct_rdep = pf_clamp(v, 0, 1e6); pf_put(0x12d0, PFT_F64, p->ct_rdep); return 1;   /* minimum 0 (+0x3d58) */
    case K(2997, 1842): p->ct_rtol = pf_clamp(v, CAM_SYS_TOL, 1e6); pf_put(0x12e0, PFT_F64, p->ct_rtol); return 1;   /* minimum tol (+0x2168) */
    case K(2997, 2067): p->ct_r1way = v != 0; pf_put(0x12d8, PFT_U8, v != 0); return 1;
    case K(2997, 1450): p->ct_rfinal = v != 0; pf_put(0x12e8, PFT_U8, v != 0); return 1;
    case K(2997, 1446): p->ct_rlin = v != 0; pf_put(0x12d9, PFT_U8, v != 0); return 1;
    case K(2996, 1695): p->chamfer_w = pf_clamp(v, 0.01, 50); return 1;
    case K(2996, 1702): p->chamfer_tip = pf_clamp(v, 0, 20); return 1;
    case K(2940, 1335): p->pk_roll = (int)v; pf_put(0x5d3, PFT_U8, v); return 1;
    case K(2942, 1855): p->face_style = (int)v; pf_put(0x1228, PFT_S16, v); return 1;   /* MapToOp FUN_18016bcb0: index -> 0 / 4 / 3 / 5 */
    /* the record fields mcam_saveops.c does not write from CamParams are patched with the edit (only then): % fields as
       stored; the face roughing angle (units unverified, notes F3) and style (this program's codes) are not written */
    /* Face % fields with their distances (store FUN_18016bcb0, %->abs FUN_18016cea0: B = A x D / 100) */
    case K(2944, 1290): case K(25592, 1290): case K(2824, 1665): p->mc_comp = (int)v; pf_put(0x5d0, PFT_S16, v); return 1;
    case K(2944, 1274): case K(25592, 1274): p->mc_comp_right = v != 0; pf_put(0x5d2, PFT_U8, v != 0); return 1;
    case K(25592, 1310): if (t != CAM_HELIX) return 0;   /* circle: record 0x1238 in its block (the generic path) */
        p->mc_tip = v != 0; pf_put(0x2f8, PFT_U8, v != 0); return 1;
    /* batch 8: Helix Bore feed pairs - the kill-focus handlers: % edit -> feed = % x 0.01 x F (FUN_18016ec60 / FUN_18016eab0), feed edit
       -> % = feed x 100 / F when F > 1e-12 (FUN_18016ebf0 / FUN_18016ea40) */
    case K(2823, 2157): case K(2823, 2159): { int po = d->id == 2157 ? 0x1268 : 0x12a0;
        pf_put(po, PFT_F64, v); pf_put(po + 8, PFT_F64, v * 0.01 * p->feed); return 1; }
    case K(2823, 2158): case K(2823, 2160): { int po = d->id == 2158 ? 0x1268 : 0x12a0;
        pf_put(po + 8, PFT_F64, v); if (p->feed > 1e-12) pf_put(po, PFT_F64, v * 100 / p->feed); return 1; }
    case K(25595, 1056): p->drill_cycle = (int)v; if (!p->drill_cycle) p->peck = 0; return 1;
    case K(25595, 1072): p->peck = pf_clamp(v, 0, 100); return 1;
    case K(25595, 1089): p->drill_dwell = pf_clamp(v, 0, 1e5); pf_put(0x1250, PFT_F64, p->drill_dwell); return 1;   /* batch 8 */
    case K(25594, 3019): p->tip_comp = v != 0; return 1;
    case K(25594, 1338): p->drill_bt = v; pf_put(0x1260, PFT_F64, v); return 1;   /* batch 8: the depth moves with it (cam_drill_z) */
    case K(2824, 1659): p->thread_pitch = pf_clamp(v, 0, 20); pf_put(0x1258, PFT_F64, p->thread_pitch); return 1;
    case K(2824, 1677): case K(2824, 1678): p->thread_internal = v == 0; pf_put(0x1290, PFT_S16, v); return 1;
    case K(2824, 1690): case K(2824, 1691): p->thread_rh = v == 0; pf_put(0x12a0, PFT_U8, v); return 1;
    case K(2824, 1697): case K(2824, 1698): p->thread_up = v != 0; pf_put(0x12a2, PFT_U8, v); return 1;
    /* 3205 / 2784 stepover 2078 % 0x1580 with distance 615 0x1250, min radius 3199 % 0x1658 with 3198 0x1650 (FUN_180168640:
       B = A x D / 100); 379's XY stepover % has no single distance partner (min / max 2206 / 2207 are their own fields) */
    case K(3205, 2078): case K(2784, 2078): p->stepover_pct = pf_clamp(v, 2, 90); pf_put(0x1580, PFT_F64, p->stepover_pct); pf_put(0x1250, PFT_F64, p->stepover_pct * D / 100); return 1;
    /* batch 9: Area Cut 379 XY stepover, the page's kill-focus handlers (decomp/mcmill_all.c; vtable 0x18033b530: +0x418 FUN_180189aa0,
       +0x420 FUN_180189b40, +0x428 FUN_1801899a0, +0x430 FUN_1801899c0): % (FUN_1801892a0): Max = % / 100 x D, then +0x430 Min = 0.55 x
       Max; Max (FUN_180189120): % = Max / D x 100 (D <= tol: +0x420 % = 45); Min (FUN_1801891d0): kept. UpdateLocalOp FUN_1801897f0
       writes 0x1580 / 0x1590 / 0x1598 and, after such an edit, 0x1588 = Min / Max x 100. The 2 .. 90 clamp of the % is this
       program's (as 3205 2078) */
    case K(379, 2216): { p->stepover_pct = pf_clamp(v, 2, 90); double mx = p->stepover_pct / 100 * D, mn = mx * 0.55;
        pf_put(0x1580, PFT_F64, p->stepover_pct); pf_put(0x1598, PFT_F64, mx); pf_put(0x1590, PFT_F64, mn);
        if (mx > 5e-5) pf_put(0x1588, PFT_F64, mn / mx * 100); return 1; }
    case K(379, 2207): { double mx = pf_clamp(v, 0, 1000), mn = pf_rv(0x1590, PFT_F64);
        pf_put(0x1598, PFT_F64, mx); p->stepover_pct = D > 5e-5 ? mx / D * 100 : 45; pf_put(0x1580, PFT_F64, p->stepover_pct);
        if (mx > 5e-5) pf_put(0x1588, PFT_F64, mn / mx * 100); return 1; }
    case K(379, 2206): { double mn = pf_clamp(v, 0, 1000), mx = pf_rv(0x1598, PFT_F64);
        pf_put(0x1590, PFT_F64, mn); if (mx > 5e-5) pf_put(0x1588, PFT_F64, mn / mx * 100); return 1; }
    case K(3205, 3199): case K(2784, 3199): p->min_rad_pct = pf_clamp(v, 0, 100); pf_put(0x1658, PFT_F64, p->min_rad_pct); pf_put(0x1650, PFT_F64, p->min_rad_pct * D / 100); return 1;
    case K(3205, 3200): case K(2784, 3200): p->micro_lift = pf_clamp(v, 0, 5); pf_put(0x16d0, PFT_F64, p->micro_lift); return 1;
    case K(3205, 616): case K(2784, 616): p->back_feed = pf_clamp(v, 0, 50000); pf_put(0x1248, PFT_F64, p->back_feed); return 1;
    case K(2784, 618): p->slot_width = pf_clamp(v, 0, 500); pf_put(0x1258, PFT_F64, p->slot_width); return 1;   /* 0 = the code default (FUN_180179370) */
    case K(3205, 3203): case K(3205, 3204):   /* LoadDynPockParm: +0x15cd ? +0x1608 % of D : +0x1600 */
        pf_put(0x15cd, PFT_U8, v); p->link_retract = v ? pf_rv(0x1608, PFT_F64) * D * 0.01 : pf_rv(0x1600, PFT_F64); return 1;
    case K(3205, 986): p->link_retract = pf_clamp(v, 0, 1000); pf_put(0x1600, PFT_F64, p->link_retract); return 1;
    case K(3205, 3205): pf_put(0x1608, PFT_F64, v); p->link_retract = v * D * 0.01; return 1;
    /* Depth Cuts (batch 3) */
    case K(25598, 3012): case K(3030, 3012): case K(25602, 3012):   /* the group: on = record 0x868 with the stored max rough step */
        p->mc_depth_on = v != 0; pf_put(0x868, PFT_U8, v != 0); p->stepdown = v ? pf_rv(0x870, PFT_F64) : 0; return 1;
    case K(25598, 1228): case K(3030, 1228): case K(25602, 1228): p->stepdown = pf_clamp(v, 0.01, 1000); return 1;
    case K(25598, 1233): case K(25602, 1233): case K(3030, 1233): p->mc_depth_nf = (int)pf_clamp(v, 0, 100);
        if (t == CAM_CONTOUR && p->mc_depth_nf > 0 && p->mc_depth_fs == 0) p->mc_depth_fs = pf_rv(0x880, PFT_F64);   /* the step shown */
        if (t == CAM_CIRCLE || t == CAM_CONTOUR || t == CAM_POCKET || t == CAM_FACE) pf_put(0x878, PFT_S16, p->mc_depth_nf); return 1;
    case K(25598, 25195): case K(25602, 25195): case K(3030, 25195): p->mc_depth_fs = pf_clamp(v, 0, 1000);
        if (t == CAM_CIRCLE || t == CAM_CONTOUR || t == CAM_POCKET || t == CAM_FACE) pf_put(0x880, PFT_F64, p->mc_depth_fs); return 1;
    case K(25598, 1377): p->mc_keep_down = v != 0; pf_put(0x891, PFT_U8, v != 0); return 1;
    case K(25598, 1381): p->dc_taper = v != 0; pf_put(0x130a, PFT_U8, v != 0); return 1;
    case K(25598, 1252): p->dc_taper_a = pf_clamp(v, 0, 89 * M_PI / 180); pf_put(0x1310, PFT_F64, p->dc_taper_a); return 1;   /* MCReal limit 89 (ctor) */
    case K(25598, 1979): case K(25598, 1980): p->dc_bydepth = v != 0; pf_put(0x1318, PFT_U8, v != 0); return 1;
    /* Lead In/Out (batch 4): the side's value, its % / distance partner (B = A x D / 100, FUN_180171320 / FUN_1801735c0),
       then both sides as record patches (pf_lead_put) */
    case K(3013, 3229): case K(2793, 3229): p->lead_on = v != 0; pf_lead_put(); return 1;
    case K(3013, 3391): case K(2793, 3391): p->lead_in_on = v != 0; pf_lead_put(); return 1;
    case K(3013, 3392): case K(2793, 3392): p->lead_out_on = v != 0; pf_lead_put(); return 1;
    case K(3013, 25167): case K(3013, 1160): case K(2793, 25167): case K(2793, 1160): p->lead_in_perp = v != 0; pf_lead_put(); return 1;
    case K(2793, 25182): case K(2793, 25188): p->lead_out_perp = v != 0; pf_lead_put(); return 1;
    case K(2793, 25324): p->lead_ovl = pf_clamp(v, 0, 1000); pf_put(0x988, PFT_F64, p->lead_ovl); pf_lead_put(); return 1;
    case K(3004, 1814): pf_put(0x13a8, PFT_S16, v); return 1;
    case K(3004, 2144): p->ct_ccopt = v != 0; pf_put(0x5d4, PFT_U8, v != 0); return 1;
    case K(3013, 25182): case K(3013, 25188): p->lead_out_perp = v != 0; pf_lead_put(); return 1;
    case K(3013, 25166): case K(2793, 25166): p->lead_len_pct = pf_clamp(v, 0, 1000); p->lead_len = p->lead_len_pct * D / 100; pf_lead_put(); return 1;
    case K(3013, 25165): case K(2793, 25165): p->lead_len = pf_clamp(v, 0, 50); p->lead_len_pct = D > 0 ? 100 * p->lead_len / D : 0; pf_lead_put(); return 1;
    case K(3013, 25172): case K(2793, 25172): p->lead_r_pct = pf_clamp(v, 0, 1000); p->lead_r = p->lead_r_pct * D / 100; pf_lead_put(); return 1;
    case K(3013, 25171): case K(2793, 25171): p->lead_r = pf_clamp(v, 0, 50); p->lead_r_pct = D > 0 ? 100 * p->lead_r / D : 0; pf_lead_put(); return 1;
    case K(3013, 25174): case K(2793, 25174): p->lead_sw = pf_clamp(v, 0, M_PI); p->lead_sw_rec |= 1; pf_lead_put(); return 1;   /* MCReal +0xbf8 = 180 */
    case K(3013, 25181): case K(2793, 25181): p->lead_xlen_pct = pf_clamp(v, 0, 1000); p->lead_xlen = p->lead_xlen_pct * D / 100; pf_lead_put(); return 1;
    case K(3013, 25180): case K(2793, 25180): p->lead_xlen = pf_clamp(v, 0, 50); p->lead_xlen_pct = D > 0 ? 100 * p->lead_xlen / D : 0; pf_lead_put(); return 1;
    case K(3013, 25186): case K(2793, 25186): p->lead_xr_pct = pf_clamp(v, 0, 1000); p->lead_xr = p->lead_xr_pct * D / 100; pf_lead_put(); return 1;
    case K(3013, 25185): case K(2793, 25185): p->lead_xr = pf_clamp(v, 0, 50); p->lead_xr_pct = D > 0 ? 100 * p->lead_xr / D : 0; pf_lead_put(); return 1;
    case K(3013, 25187): case K(2793, 25187): p->lead_xsw = pf_clamp(v, 0, M_PI); p->lead_sw_rec |= 2; pf_lead_put(); return 1;
    case K(3013, 25369): p->mc_lead_dz[0] = pf_clamp(v, 0, 1000); pf_lead_put(); return 1;
    case K(3013, 25219): p->mc_lead_dz[1] = pf_clamp(v, 0, 1000); pf_lead_put(); return 1;
    case K(3013, 25370): p->mc_lead_dz[2] = pf_clamp(v, 0, 1000); pf_lead_put(); return 1;
    case K(3013, 25220): p->mc_lead_dz[3] = pf_clamp(v, 0, 1000); pf_lead_put(); return 1;
    case K(3013, 25324): p->lead_ovl = pf_clamp(v, 0, 1000); pf_put(0x988, PFT_F64, p->lead_ovl); pf_lead_put(); return 1;
    case K(3013, 25158): p->lead_mid = v != 0; pf_put(0x990, PFT_U8, v != 0); pf_lead_put(); return 1;
    case K(3013, 25200): p->lead_gouge = v != 0; pf_put(0x991, PFT_U8, v != 0); pf_lead_put(); return 1;
    case K(3013, 25169): p->lead_xflags = (p->lead_xflags & ~2) | (v != 0 ? 2 : 0); pf_put(0x929, PFT_U8, v != 0); pf_lead_put(); return 1;
    case K(3013, 25170): p->lead_xflags = (p->lead_xflags & ~64) | (v != 0 ? 64 : 0); pf_put(0x92a, PFT_U8, v != 0); pf_lead_put(); return 1;
    case K(3013, 25183): p->lead_xflags = (p->lead_xflags & ~8) | (v != 0 ? 8 : 0); pf_put(0x971, PFT_U8, v != 0); pf_lead_put(); return 1;
    case K(3013, 25184): p->lead_xflags = (p->lead_xflags & ~128) | (v != 0 ? 128 : 0); pf_put(0x972, PFT_U8, v != 0); pf_lead_put(); return 1;
    case K(3013, 1225): p->adj_s = v != 0; pf_put(0x1320, PFT_U8, v != 0); pf_lead_put(); return 1;
    case K(3013, 1226): p->adj_e = v != 0; pf_put(0x1338, PFT_U8, v != 0); pf_lead_put(); return 1;
    case K(3013, 1300): case K(3013, 1164): p->adj_s_ext = v != 0; pf_put(0x1321, PFT_U8, v != 0); pf_lead_put(); return 1;
    case K(3013, 1304): case K(3013, 1167): p->adj_e_ext = v != 0; pf_put(0x1339, PFT_U8, v != 0); pf_lead_put(); return 1;
    case K(3013, 1990): p->adj_s_pct = pf_clamp(v, 0, 1000); p->adj_s_len = p->adj_s_pct * D / 100; pf_put(0x1330, PFT_F64, p->adj_s_pct); pf_put(0x1328, PFT_F64, p->adj_s_len); pf_lead_put(); return 1;
    case K(3013, 1302): p->adj_s_len = pf_clamp(v, 0, 1000); p->adj_s_pct = D > 0 ? 100 * p->adj_s_len / D : 0; pf_put(0x1330, PFT_F64, p->adj_s_pct); pf_put(0x1328, PFT_F64, p->adj_s_len); pf_lead_put(); return 1;
    case K(3013, 1992): p->adj_e_pct = pf_clamp(v, 0, 1000); p->adj_e_len = p->adj_e_pct * D / 100; pf_put(0x1348, PFT_F64, p->adj_e_pct); pf_put(0x1340, PFT_F64, p->adj_e_len); pf_lead_put(); return 1;
    case K(3013, 1306): p->adj_e_len = pf_clamp(v, 0, 1000); p->adj_e_pct = D > 0 ? 100 * p->adj_e_len / D : 0; pf_put(0x1348, PFT_F64, p->adj_e_pct); pf_put(0x1340, PFT_F64, p->adj_e_len); pf_lead_put(); return 1;
    case K(3271, 3237): p->mc_lead_on = v != 0; pf_put(0x8f0, PFT_U8, v != 0); return 1;
    case K(2837, 1650): p->clearance = pp_hf.clr_abs = v; pp_hf.clr_on = 1; if (pp_hf.thr_clr_on) pp_hf.thr_clr = v; return 1;   /* kept by hf_apply */
    case K(2837, 1651): case K(2837, 1653): case K(2837, 1654): {
        double zc = (int)pf_rv(0x1250, PFT_S16) == 1 ? pp_hf.cz : 0, fpa = p->top_z + p->feed_plane;
        if (d->id == 1651) p->feed_plane = v + zc - p->top_z;
        else if (d->id == 1653) { p->top_z = v + zc; p->feed_plane = fpa - p->top_z; }   /* the feed plane keeps its Z */
        else p->final_z = v + zc;
        return 1; }
    case K(2837, 1059): pp_hf.clr_end = v != 0; return 1;
    case K(2837, 1655): case K(2837, 1656): {   /* batch 8: the shown values stay, their meaning changes (MapToOp writes them as typed) */
        double zo = (int)pf_rv(0x1250, PFT_S16) == 1 ? pp_hf.cz : 0, zn = (int)v == 1 ? pp_hf.cz : 0;
        double fpa = p->top_z + p->feed_plane;
        p->top_z += zn - zo; p->final_z += zn - zo; p->feed_plane = fpa + zn - zo - p->top_z;
        pf_put(0x1250, PFT_S16, (int)v == 1 ? 1 : 0); return 1; }
    case K(25592, 1735): if (!p->mc_ok) return 0; pf_put(t == CAM_HELIX ? 0x1240 : 0x1258, PFT_F64, pf_clamp(v, 0, 1e5)); return 1;
    case K(25591, 1271): { int fn = (int)pf_clamp(v, 0, 10); if ((fn > 0) != (p->finish_n > 0)) pp_mp_pic = fn > 0; p->finish_n = fn; return 1; }   /* OnBnClickedMultipassCb picture */
    case K(25591, 1275): p->finish_step = pf_clamp(v, 0.01, 10); return 1;
    case K(25591, 1264): p->mc_mp_nr = (int)pf_clamp(v, 0, 100); pf_put(0x8aa, PFT_S16, p->mc_mp_nr); return 1;
    case K(25591, 1267): p->mc_mp_rs = pf_clamp(v, 0, 1000); pf_put(0x8b0, PFT_F64, p->mc_mp_rs); return 1;
    case K(25591, 1260): case K(25591, 1262): p->ct_fin_all = v != 0; pf_put(0x1230, PFT_U8, v != 0); return 1;
    case K(2825, 2945): case K(3270, 2945): p->mc_mp_on = v != 0; pf_put(0x8a8, PFT_U8, v != 0); break;
    case K(2825, 1264): case K(3270, 1264): p->mc_mp_nr = (int)pf_clamp(v, 0, 100); pf_put(0x8aa, PFT_S16, p->mc_mp_nr); break;
    case K(2825, 1267): case K(3270, 1267): p->mc_mp_rs = pf_clamp(v, 0, 1000); pf_put(0x8b0, PFT_F64, p->mc_mp_rs); break;
    case K(2825, 1271): case K(3270, 1271): p->mc_mp_nf = (int)pf_clamp(v, 0, 100); pf_put(0x8b8, PFT_S16, p->mc_mp_nf); break;
    case K(2825, 1275): case K(3270, 1275): p->mc_mp_fs = pf_clamp(v, 0, 1000); pf_put(0x8c0, PFT_F64, p->mc_mp_fs); break;
    case K(2834, 1111): p->tab_n = (int)pf_clamp(v, 0, 12); if (p->tabs > 0) p->tabs = p->tab_n > 0 ? p->tab_n : 1;
        pf_put(0x1382, PFT_S16, p->tab_n); return 1;   /* 0x1382 (the generator's tab_n); the 0..12 range is ours */
    case K(2834, 1085): p->tab_w = pf_clamp(v, 0.1, 100); return 1;
    case K(2834, 1088): p->tab_h = pf_clamp(v, 0.05, 50); return 1;
    case K(3003, 25329): p->stepover_pct = pf_clamp(v, 5, 95); pf_put(0x1240, PFT_F64, p->stepover_pct * p->tool_dia / 100); return 1;
    case K(3003, 1364): if (p->tool_dia <= 0) return 1; p->stepover_pct = pf_clamp(v * 100 / p->tool_dia, 5, 95);   /* batch 6: A = B x 100 / D */
        pf_put(0x1240, PFT_F64, p->stepover_pct * p->tool_dia / 100); return 1;
    case K(3003, 1360): p->pocket_no_rough = v == 0; pf_put(0x1238, PFT_U8, v != 0); return 1;
    case K(3003, 1419): p->pocket_style = v == 0 ? 1 : 0; pf_put(0x123c, PFT_S16, v); return 1;   /* params_from_record: 0 = zigzag */
    case K(3003, 1366): p->pocket_angle = v; pf_put(0x1250, PFT_F64, v * M_PI / 180); return 1;   /* batch 6: shown in degrees, the record
                                                       holds radians (rough_angle, process_pocket l.323238; Pockzigr cos / sin) */
    case K(3004, 1361): pf_put(0x1239, PFT_U8, v != 0); p->finish_n = v != 0 ? (int)pf_rv(0x125a, PFT_S16) : 0; return 1;   /* Open pocket cutting method only (the control is live only then) */
    case K(3004, 1369): p->finish_n = (int)pf_clamp(v, 0, 10); return 1;
    case K(3004, 1370): p->finish_step = pf_clamp(v, 0.01, 10); return 1;
    case K(3010, 1488): { double deg = pf_clamp(v * 180 / M_PI, 0.5, 90); p->ramp_deg = deg; if (p->pk_ok) pf_put(0x12c0, PFT_F64, deg * M_PI / 180); return 1; }
    case K(3020, 2079): p->stepover_pct = pf_clamp(v, 5, 95); pf_put(0x1268, PFT_F64, p->stepover_pct); pf_put(0x1270, PFT_F64, p->stepover_pct * D / 100); return 1;   /* + the distance (FUN_18018e050) */
    case K(3020, 2083): p->ramp_deg = pf_clamp(v, 0.5, 90); pf_put(0x1278, PFT_F64, p->ramp_deg); return 1;
    case K(3020, 2077): if (!(D > 0)) return 1;   /* batch 7: distance -> % (FUN_18018e050 kill-focus: % = distance x 100 / D), the engine reads the % */
        p->stepover_pct = pf_clamp(v * 100 / D, 5, 95); pf_put(0x1268, PFT_F64, p->stepover_pct); pf_put(0x1270, PFT_F64, p->stepover_pct * D / 100); return 1;
    case K(2823, 1700): p->helix_pitch = pf_clamp(v, 0.01, 50); pf_put(0x1250, PFT_F64, p->helix_pitch); return 1;
    case K(2823, 1439): p->finish_n = v != 0; pf_put(0x1278, PFT_U8, p->finish_n); return 1;
    case K(2823, 1732): p->finish_step = pf_clamp(v, 0.01, 10); pf_put(0x1288, PFT_F64, p->finish_step); return 1;
    case K(3219, 3216): p->ramp_deg = pf_clamp(v, 0.5, 90); pf_put(0x1678, PFT_F64, p->ramp_deg); return 1;   /* degrees (notes F4) */
    case K(3219, 3206): pf_put(0x16bb, PFT_U8, (int)v); return 1;   /* batch 9 */
    default: return 0;
    }
    if (t == CAM_THREAD) pf_thread_sync(p);                   /* the 2825 fields (break above) */
    return 1;
}
/* combos whose items this program offers (the generators' choices), else the decoded items */
static int pf_bitems(const PfDef *d, const char **it, int *code) {
    const CamParams *p = &pending.p; int n = 0;
    #define IT(C_, S_) (it[n] = (S_), code[n] = (C_), n++)
    switch (K(d->dlg, d->id)) {
    case K(2938, 1290): case K(3025, 1290): case K(2944, 1290): case K(25592, 1290): case K(2824, 1665): {
        /* mccoreres 4589 Computer / 4591 Control / 4593 Wear / 4595 Reverse Wear / 4597 Off (the CCutterCompComboBox fill is
           NOT traced, formats/opdlg_port_audit.md 6.3; index = record value, 4 = Off); offered: Computer / Off - the
           LinuxCNC post writes no G41 / G42 (ours, formats/opdlg_port_audit.md 5 batch 5); a stored Control / Wear / Reverse
           Wear shows greyed */
        double v; int live; if (!pf_bget(d, &v, &live)) return 0;
        static const char *cc[5] = { "Computer", "Control", "Wear", "Reverse Wear", "Off" };
        if ((!live && ((d->dlg != 2938 && d->dlg != 3025) || p->ct_comp)) || ((d->dlg == 2938 || d->dlg == 3025) && pf_cc_ok()) || ((d->dlg == 2944 || d->dlg == 2824) && nc_cutter_comp_on())) { for (int i = 0; i < 5; i++) IT(i, cc[i]); return n; }   /* batch 5: contour
                                                    with MCAM_CUTTER_COMP=1, or a stored Control / Wear / Reverse Wear (Engrave: ours, the two) */
        IT(0, "Computer"); IT(4, "Off"); return n; }
    case K(2938, 2064):   /* contour type: 2D (mcmillres 1229) / 2D chamfer (1234), the generator's two; Engrave is ours */
        if (p->contour_type == 2) { IT(0, "Engrave"); return n; }
        IT(0, "2D"); IT(2, "2D chamfer"); IT(4, "Ramp"); IT(5, "Remachining"); IT(6, "Oscillate");   /* Mastercam's list order; mcmillres 1240 Ramp / 1247 Oscillate */
        { static const char *tn[7] = { "", "3D", "", "3D chamfer", "", "Remachining", "" };   /* a stored other type (refused) */
          if (p->contour_type == 0 && p->ct_rtype > 0 && p->ct_rtype < 7 && p->ct_rtype != 2 && p->ct_rtype != 4 && p->ct_rtype != 5 && p->ct_rtype != 6) IT(p->ct_rtype, tn[p->ct_rtype]); }
        return n;
    case K(3219, 3206): {   /* batch 9: the entry methods the ported engine runs (mcce_dyn.c plunge_entry): 0x100 Helix only = code 0,
                               0x800 Profile = code 3; medax_de: 1 / 2 / 4 (0x200 / 0x400 / 0x1000, mcce_medent.c on the CalcMedAx port)
                               too. 5 (Custom entry chain: the chain source and FUN_180132200 NOT DECODED, the engine refuses it) is
                               listed only when the op holds it. Names: the template's combo strings (cui_opfields.inc 3206) */
        int m = (int)pf_rv(0x16bb, PFT_U8);
        static const char *em[6] = { "Helix only", "Helix followed by full medial burial", "Helix followed by trochoid medial", "Profile", "Medial", "Custom, use entry chain" };
        for (int i = 0; i < 5; i++) IT(i, em[i]);
        if (m == 5) IT(5, em[5]);
        return n; }
    case K(2942, 1855):   /* mcmillres 2676 / 2677 / 2679 / 2675 in the combo's order (FUN_18016ba50; 2678 Spiral not offered) */
        IT(0, "Zigzag"); IT(4, "One way"); IT(3, "One pass"); IT(5, "Dynamic"); return n;
    case K(25595, 1056): {   /* the cycle names: post text page 0x27 entry 1 + 12 n (DrillCyclePageBase::InitPage l.444621; batch 8:
                                nc_drill_text, the Generic Fanuc 3X Mill post's text over mccoreres 7645 + 11 n); the cycles this
                                program posts (0-2, with MCAM_DRILL_CYCLES=1 0-4), all eight for a stored other cycle */
        int mx = pf_drill_maxcyc();
        for (int i = 0; i <= mx; i++) IT(i, nc_drill_text(i, 1, NULL));
        if (p->drill_cycle > mx && p->drill_cycle <= 19) IT(p->drill_cycle, nc_drill_text(p->drill_cycle, 1, NULL));
        return n; }
    }
    #undef IT
    return 0;
}

/* ---- custom fields (several record bytes per control; the tsv rows marked "custom" by gen_opdlg_fields.py) */
/* ---- batch 11: the post-side pages (cam_postvals.c; formats/opdlg_port_audit.md 17) */
/* the machine whose coolant definition the Coolant page shows: the part's, else (a new part) the operation defaults library's
   machine, the base a new part is saved from (cui_toolpage.c tl6410_mach, as the Tool List takes it) */
static const McamFile *PV_MACH(void) { pv_set_machine_fallback(tl6410_mach); return pv_machine(&F); }
/* Coolant 6546: per option k the state combo (DDX 0xc33 / 0xc34 / 0xc36 ... 0xc44) and its Before / With / After combo (0x379 / 0xc35 /
   0xc37 ... 0xc45), CoolantPageBase::DoDataExchange 180232d20 */
static const int COOL_S[10] = { 3123, 3124, 3126, 3128, 3130, 3132, 3134, 3136, 3138, 3140 };
static const int COOL_P[10] = { 889, 3125, 3127, 3129, 3131, 3133, 3135, 3137, 3139, 3141 };
static int cool_k(int id, int *pos) {
    for (int k = 0; k < 10; k++) { if (COOL_S[k] == id) { *pos = 0; return k; } if (COOL_P[k] == id) { *pos = 1; return k; } }
    return -1;
}
static void cool_arr(int16_t can[20]) { for (int i = 0; i < 20; i++) can[i] = (int16_t)pf_rv(PV_CAN + 2 * i, PFT_S16); }
/* the V9 coolant word as the op will be saved: the dialog's / op's patch, else mcam_saveops.c's rule for the op's tool block +0x50
   (= record 0x398): 16 when CamParams.coolant (the tool's flood), 8 instead of a stored 16 when not, any other value kept */
static int16_t cool_oldw(void) {
    int16_t w = (int16_t)pf_rv(PV_COOL_OLD, PFT_S16); int pat = 0;
    for (int k = 0; k < pf_ned; k++) if (pf_ed[k].off == PV_COOL_OLD) pat = 1;
    for (int k = 0; k < pending.p.nrpatch; k++) if (pending.p.rpatch[k].off == PV_COOL_OLD) pat = 1;
    if (!pat) { if (pending.p.coolant) w = 0x10; else if (w == 0x10) w = 8; }
    return w;
}
static void cool_get(PvCool *c) { int16_t can[20]; cool_arr(can); pv_cool_decode(PV_MACH(), can, cool_oldw(), c); }
/* Quick View "Coolant" (CoolantPageBase::UpdateQuickViewData 180237840 -> IsCoolantEnabled 1802331b0: any option On) */
int pv_qv_coolant(void) { PvCool c; cool_get(&c); for (int k = 0; k < 10; k++) if (c.state[k] == 0) return 1; return 0; }
/* UpdateCannedText 180236f10 (+ MapCoolantToCannedText): the changed shorts as record edits; the V9-style word also sets CamParams.coolant
   (the Tool page's flood flag, saved as the op's tool block +0x50 = record 0x398 by mcam_saveops.c) */
static void cool_put(const PvCool *c) {
    int16_t can[20], can0[20]; cool_arr(can); memcpy(can0, can, sizeof can0);
    int16_t ow0 = cool_oldw(), ow = ow0; uint8_t on0 = (uint8_t)pf_rv(PV_CAN_ON, PFT_U8), on = on0;
    pv_cool_encode(PV_MACH(), c, can, &ow, &on);
    for (int i = 0; i < 20; i++) if (can[i] != can0[i]) pf_put(PV_CAN + 2 * i, PFT_S16, can[i]);
    if (ow != ow0) { pf_put(PV_COOL_OLD, PFT_S16, ow); pending.p.coolant = ow == 0x10; }
    if (on != on0) pf_put(PV_CAN_ON, PFT_U8, on);
}
/* Misc Values 6555: the value edits (DDX 0x897 / 0x899 / 0x89b-0x8a2 ints, 0x8a3-0x8ac reals) and the description edits (SetDialogText
   18052ffe0: 0x8ad-0x8b5 / 0x8b7 = post text entries 1-10, 0x8b8 / 0x8ba / 0x8bc / 0x8be / 0x8c0 / 0x8c2-0x8c6 = entries 12-21) */
static int misc_val_idx(int id) {
    static const int I[10] = { 2199, 2201, 2203, 2204, 2205, 2206, 2207, 2208, 2209, 2210 };
    for (int i = 0; i < 10; i++) if (I[i] == id) return i + 1;
    return id >= 2211 && id <= 2220 ? 12 + id - 2211 : 0;
}
static int misc_desc_idx(int id) {
    static const int D[20] = { 2221, 2222, 2223, 2224, 2225, 2226, 2227, 2228, 2229, 2231, 2232, 2234, 2236, 2238, 2240, 2242, 2243, 2244, 2245, 2246 };
    for (int i = 0; i < 20; i++) if (D[i] == id) return i < 10 ? i + 1 : i + 2;
    return 0;
}
/* HideDisableButtons 18052e0c0: with "Automatically set to post values" on, OnPostValsPb 18052f460 puts the post text's values in
   (CMiscInit::InitToPostText, mcmill l.291431: GetMiscIntValueFromIndex(page 0x25, 1-10), GetMiscReal*ValueFromIndex(12-21)) */
static void misc_post_values(void) {
    for (int i = 0; i < 10; i++) {
        double vi = pv_misc_value(i + 1), vr = pv_misc_value(12 + i);
        if ((int)pf_rv(PV_MISC_INT + 4 * i, PFT_S32) != (int)vi) pf_put(PV_MISC_INT + 4 * i, PFT_S32, vi);
        if (pf_rv(PV_MISC_REAL + 8 * i, PFT_F64) != vr) pf_put(PV_MISC_REAL + 8 * i, PFT_F64, vr);
    }
}
/* Home / Ref. Points 6554: the flag bits of op +0x2b0 (record 0x318), ReferencePointsPageBase::MapToDlg 1805f2840 */
static int ref_bit(int id) {
    switch (id) { case 2483: return 1; case 2487: return 2; case 2491: return 3; case 2495: return 5; case 2499: return 6; case 2504: return 7; }
    return -1;
}
/* 3drough: Dynamic OptiRough 1465 (MapToDlg FUN_180091f20 / UpdateLocalOp FUN_180093c30): the % edits are abs / D x 100 (written back as
   abs = % x D / 100), the cut method combo (0x16e4 Conventional, 0x1728 Zigzag), the stepup / stepdown ordering combos, Mill vertical
   walls (the sign of 0x14a0) and tip compensation (index = (0x2f8 == 0)) */
static int pf_opti_custom(const PfDef *d, double *v, int *live) {
    const CamParams *p = &pending.p; double D = p->tool_dia > 0 ? p->tool_dia : 1;
    *live = 0;
    switch (d->id) {
    case 1466: *v = pf_rv(0x1230, PFT_F64) / D * 100; *live = 1; return 1;
    case 1467: *v = pf_rv(0x1228, PFT_F64) / D * 100; *live = 1; return 1;
    case 1469: *v = pf_rv(0x1478, PFT_F64) / D * 100; return 1;
    case 1472: *v = pf_rv(0x1480, PFT_F64) / D * 100; *live = 1; return 1;
    case 1239: *v = pf_rv(0x1728, PFT_U8) ? 2 : pf_rv(0x16e4, PFT_U8) ? 1 : 0; *live = 1; return 1;
    case 1486: { int a = (int)pf_rv(0x16c0, PFT_U8), b = (int)pf_rv(0x172d, PFT_U8); *v = a == 1 && b == 0 ? 2 : a == 1 && b == 1 ? 1 : 0; return 1; }
    case 1489: { int a = (int)pf_rv(0x1660, PFT_S16); *v = a == 0 ? 1 : a == 1 ? 2 : 0; return 1; }
    case 1481: *v = pf_rv(0x14a0, PFT_F64) < 0; return 1;
    case 692: *v = pf_rv(0x2f8, PFT_U8) == 0; return 1;
    }
    return 0;
}
/* Waterline page 1324 (MapToDlg FUN_1800a3ea0 / FUN_18009cdc0): the cut method from 0x16bd / 0x16be, the tip comp index */
static int pf_wl_custom(const PfDef *d, double *v, int *live) {
    *live = 0;
    switch (d->id) {
    case 1239: *v = pf_rv(0x16bd, PFT_U8) ? 2 : pf_rv(0x16be, PFT_U8) == 0 ? 1 : 0; *live = 1; return 1;
    case 692: *v = pf_rv(0x2f8, PFT_U8) == 0; return 1;
    }
    return 0;
}
/* Raster page 1330 (InitPage FUN_180088180): the cut method from 0x16d4 / 0x16bd / 0x16d5 */
static int pf_ra_custom(const PfDef *d, double *v, int *live) {
    *live = 0;
    switch (d->id) {
    case 1239: { int dm = (int)pf_rv(0x16d4, PFT_U8); *v = dm == 1 ? 3 : dm == 2 ? 4 : pf_rv(0x16bd, PFT_U8) ? 2 : pf_rv(0x16d5, PFT_U8) ? 1 : 0; *live = 1; return 1; }
    case 692: *v = pf_rv(0x2f8, PFT_U8) == 0; return 1;
    }
    return 0;
}
/* Area Roughing page 1326 (MapToDlg FUN_180087460 / FUN_18009cdc0): the cut method index = (0x16e4 != 0) (l.88380-88390), Optimize cut order
   = 2 when 0x16c1 else (0x16c0 != 0) (l.102680-102684), the tip comp index */
static int pf_ar_custom(const PfDef *d, double *v, int *live) {
    *live = 0;
    switch (d->id) {
    case 1239: *v = pf_rv(0x16e4, PFT_U8) != 0; *live = 1; return 1;
    case 1209: *v = pf_rv(0x16c1, PFT_U8) ? 2 : pf_rv(0x16c0, PFT_U8) != 0; return 1;
    case 692: *v = pf_rv(0x2f8, PFT_U8) == 0; return 1;
    }
    return 0;
}
static int pf_custom(const PfDef *d, double *v, int *live) {
    const CamParams *p = &pending.p; *live = 0;
    if (d->dlg == 3001 && d->id == 1718) { *v = pf_rv(0x13e0, PFT_F64) * 0.01 * p->tool_dia; *live = 1; return 1; }   /* FUN_180180e00 l.267827: the distance shown = % x D x 0.01 (op +0x23d8) */
    if (d->dlg == 1326 && p->type == CAM_AREAROUGH) return pf_ar_custom(d, v, live);
    if (d->dlg == 1327 && p->type == CAM_HORIZAREA) {   /* HSTHorizAreaParamPage 1327 (formats/horizontal_area.md 5): the base page's cut method / tip comp */
        if (d->id == 1302) { *v = pf_rv(0x1558, PFT_S32) + 1; *live = 1; return 1; }   /* MapToDlg FUN_18008bc90 ms l.91544: n = 0x1558 + 1 */
        return pf_ar_custom(d, v, live); }
    if (d->dlg == 1465 && p->type == CAM_OPTIROUGH) return pf_opti_custom(d, v, live);
    if (d->dlg == 1324 && p->type == CAM_WATERLINE) return pf_wl_custom(d, v, live);
    if (d->dlg == 1803 && p->type == CAM_ENGRAVE && d->id == 1065) { *v = 100 - pf_rv(0x12f8, PFT_F64); *live = 1; return 1; }   /* engrave.dll
                                                    pagexfer.c l.19 / l.97: S+0x08 holds the overlap = 100 - stepover % */
    if (d->dlg == 1002 && p->type == CAM_SFPROJECT && d->id == 997) {   /* Finish Project: the source op 0x1418 (OK l.157960-157970) */
        int id = (int)pf_rv(0x1418, PFT_S32), ok = 0; for (int i = 0; i < F.nops; i++) if (F.ops[i].id == id) ok = 1;
        *v = ok ? id : 0; *live = 1; return 1; }
    if (d->dlg == 1037 && p->type == CAM_SRPROJECT && d->id == 997) {   /* Rough Project: the source op 0x14e8 (OK FUN_1800f6c20 l.158013-158022) */
        int id = (int)pf_rv(0x14e8, PFT_S32), ok = 0; for (int i = 0; i < F.nops; i++) if (F.ops[i].id == id) ok = 1;
        *v = ok ? id : 0; *live = 1; return 1; }
    if (d->dlg == 1517 && p->type == CAM_HSTPROJECT) switch (d->id) {   /* HST Project page 1517 (OK FUN_180097920 ms l.98845-98870) */
    case 692: *v = pf_rv(0x2f8, PFT_U8) == 0; return 1;
    case 1489: *v = pf_rv(0x1558, PFT_S32) + 1; *live = 1; return 1;   /* stored = count - 1 */
    case 997: { int id = (int)pf_rv(0x157c, PFT_S32), ok = 0; for (int i = 0; i < F.nops; i++) if (F.ops[i].id == id) ok = 1;
        *v = ok ? id : 0; *live = 1; return 1; }   /* not an op of this part -> none (the OK's GetOperationPtr test) */
    }
    if (d->dlg == 240 && p->type == CAM_SFPENCIL) switch (d->id) {   /* Finish Pencil page 240 (CMsPnclPg init FUN_1800ebaa0 ms l.152240-152340) */
    case 530: { int k = ((int)pf_rv(0x1322, PFT_S16) - 1) / 2; *v = k < 1 ? 1 : k; *live = 1; return 1; }   /* (stored - 1) / 2, at least 1 */
    case 634: { double a = pf_rv(0x1338, PFT_F64); *v = a < mc_systol.tol ? 165.0 : a; *live = 1; return 1; }   /* below tol_exref: 165 */
    }
    if (d->dlg == 1329 && p->type == CAM_PENCIL) switch (d->id) {   /* Pencil page 1329 (formats/pencil.md 4) */
    case 1239: return pf_ra_custom(d, v, live);           /* the base page's cut method */
    case 1251: *v = pf_rv(0x16cb, PFT_U8) != 2; *live = 1; return 1;   /* InitPage FUN_180096430: 0x16cb 0 / 1 -> checked, 2 -> unchecked */
    case 1250: *v = pf_rv(0x1540, PFT_S32); *live = pf_rv(0x16cb, PFT_U8) != 2; return 1;
    case 706: { double so = pf_rv(0x1230, PFT_F64); int nn = (int)pf_rv(0x1540, PFT_S32);   /* not stored: FUN_180096200 */
        *v = 2 * ((nn + 1) * so + p->tool_dia / 2); *live = pf_rv(0x16cb, PFT_U8) != 2; return 1; }
    }
    if ((d->dlg == 1330 && p->type == CAM_RASTER) || (d->dlg == 1338 && p->type == CAM_SPIRAL) || (d->dlg == 1339 && p->type == CAM_RADIAL) || (d->dlg == 1328 && p->type == CAM_SCALLOP)) return pf_ra_custom(d, v, live);   /* Spiral / Radial: Raster's base page (FUN_180088180) */
    switch (K(d->dlg, d->id)) {
    case K(3005, 3009): case K(3005, 3008): case K(3005, 3007): {   /* HSTPocketEntryPage load FUN_18017cf40: Off when 0x12a0 = 0,
                                                                       else Helix (0x12a2 = 0) / Ramp (1) */
        int on = (int)pf_rv(0x12a0, PFT_U8), ty = (int)pf_rv(0x12a2, PFT_S16), e = !on ? 3007 : ty == 1 ? 3008 : 3009;
        *v = e == d->id; *live = p->pk_ok; return 1; }
    case K(2823, 3405): {   /* finish method: 0 Helix upwardly (both 0), 1 Helix downwardly (0x12c0 = 1), 2 Circle (0x12c1 = 1); InitPage FUN_18016e480 */
        *v = pf_rv(0x12c1, PFT_U8) ? 2 : pf_rv(0x12c0, PFT_U8) ? 1 : 0; *live = p->mc_ok && p->finish_n > 0; return 1; }
    case K(3205, 3328): case K(2784, 3328): {   /* FUN_180167720: style 2 = Left / Right of 0x5d2; else Climb / Conventional / Zigzag of
                                                   0x1564 and 0x1784 bit 1 */
        if (pf_style() == 2) *v = pf_rv(0x5d2, PFT_U8) != 0;
        else *v = ((int)pf_rv(0x1784, PFT_S32) & 2) ? 2 : pf_rv(0x1564, PFT_U8) != 0;
        /* batch 9: live where the engine runs: Dynamic Mill (LoadDynPockParm: flags 1 = (0x1564 == 0), 4 = 0x1784 bit 1), Dynamic
           Contour (flags 0x400000 from 0x5d2), Peel Mill (style 3: LoadDynPockParm as Dynamic Mill; HST engines) */
        *live = p->type == CAM_DYNAMIC || p->type == CAM_DYNCONTOUR || (p->type == CAM_PEEL && pf_rv(0x17d8, PFT_U8) == 0);   /* 2784: FUN_180179de0 */
        return 1; }
    case K(1337, 25618): *v = 2 - pf_rv(0x16fe, PFT_U8); return 1;   /* "index = 2 - stored" */
    }
    if (d->dlg == 6546) {   /* batch 11: CoolantPageBase SetDialogData 180234fd0 / SetControlsState 180234b10 */
        int pos, k = cool_k(d->id, &pos); if (k < 0) return 0;
        PvCool c; cool_get(&c); int fp = pv_cool_frompost(PV_MACH()), en = pv_cool_enabled(PV_MACH(), k);
        if (!pos) { *v = c.state[k];   /* V9 style: an option is off while another of Flood / Mist / Thru-tool is On (SetDialogData / OnSelchange*OOICmb) */
            int other = 0; for (int j = 0; j < 3; j++) if (j != k && c.state[j] == 0) other = 1;
            *live = en && !(fp && other); }
        else { *v = c.pos[k]; *live = !fp && en && c.state[k] != 2; }
        return 1;
    }
    if (d->dlg == 6548 && d->id >= 3162 && d->id <= 3165) { *v = -1; *live = 0; return 1; }   /* batch 11: lists, display only */
    if (d->dlg == 6554) {   /* batch 11: MapToDlg - all three axis bits 0 -> all three shown set */
        int fl = (int)pf_rv(0x318, PFT_U8), b = ref_bit(d->id); *live = 0;
        if (b > 0) { int base = b < 4 ? 1 : 5; *v = (fl >> base & 7) == 0 ? 1 : (fl >> b & 1); return 1; }
        switch (d->id) { case 2696: *v = !(fl & 1); return 1; case 2698: *v = fl & 1; return 1; case 3447: *v = !(fl >> 4 & 1); return 1; case 2703: *v = fl >> 4 & 1; return 1; }
    }
    return 0;
}
static void pf_custom_set(const PfDef *d, double v) {
    if (d->dlg == 1465 && pending.p.type == CAM_OPTIROUGH) {   /* 3drough: UpdateLocalOp FUN_180093c30 */
        double D = pending.p.tool_dia > 0 ? pending.p.tool_dia : 1;
        switch (d->id) {
        case 1466: pf_put(0x1230, PFT_F64, v * D / 100); return;
        case 1467: pf_put(0x1228, PFT_F64, v * D / 100); return;
        case 1472: pf_put(0x1480, PFT_F64, v * D / 100); return;
        case 1239: pf_put(0x16e4, PFT_U8, (int)v == 1); pf_put(0x1728, PFT_U8, (int)v == 2); return;
        }
        return;
    }
    if (d->dlg == 1037 && pending.p.type == CAM_SRPROJECT && d->id == 997) { pf_put(0x14e8, PFT_S32, (int)v); return; }
    if (d->dlg == 1002 && pending.p.type == CAM_SFPROJECT && d->id == 997) { pf_put(0x1418, PFT_S32, (int)v); return; }
    if (d->dlg == 1803 && pending.p.type == CAM_ENGRAVE && d->id == 1065) { pf_put(0x12f8, PFT_F64, 100 - v); return; }   /* pagexfer.c l.97 */
    if (d->dlg == 1517 && pending.p.type == CAM_HSTPROJECT) {
        if (d->id == 1489) pf_put(0x1558, PFT_S32, v < 1 ? 0 : (int)v - 1);
        if (d->id == 997) pf_put(0x157c, PFT_S32, (int)v);
        return; }
    if (d->dlg == 240 && pending.p.type == CAM_SFPENCIL) {   /* Finish Pencil page 240: OK FUN_1800ec2c0 ms l.152485-152540 */
        if (d->id == 530) pf_put(0x1322, PFT_S16, 2 * (int)v + 1);
        if (d->id == 634) { double t = mc_systol.tol; pf_put(0x1338, PFT_F64, v < t || v > 180.0 - t || fabs(v - 165.0) <= t ? 0.0 : v); }
        return; }
    if (d->dlg == 1329 && pending.p.type == CAM_PENCIL) {   /* Pencil page 1329: OK FUN_180096920 / FUN_180096240 */
        int nn = (int)pf_rv(0x1540, PFT_S32);
        if (d->id == 1239) { int i = (int)v; pf_put(0x16d4, PFT_U8, i == 3 ? 1 : i == 4 ? 2 : 0); pf_put(0x16bd, PFT_U8, i == 2); pf_put(0x16d5, PFT_U8, i == 1); }
        if (d->id == 1251) pf_put(0x16cb, PFT_U8, v != 0 ? (nn != 0) : 2);   /* checked -> (N != 0), unchecked -> 2 */
        if (d->id == 1250) { int k = v < 0 ? 0 : (int)v; pf_put(0x1540, PFT_S32, k); if (pf_rv(0x16cb, PFT_U8) != 2) pf_put(0x16cb, PFT_U8, k != 0); }
        if (d->id == 706) { double so = pf_rv(0x1230, PFT_F64), D = pending.p.tool_dia;   /* N = ceil((ref / 2 - D / 2) / stepover) */
            if (so > 0) { int k = (int)ceil((v / 2 - D / 2) / so); if (k < 0) k = 0; pf_put(0x1540, PFT_S32, k); if (pf_rv(0x16cb, PFT_U8) != 2) pf_put(0x16cb, PFT_U8, k != 0); } }
        return; }
    if (((d->dlg == 1330 && pending.p.type == CAM_RASTER) || (d->dlg == 1338 && pending.p.type == CAM_SPIRAL) || (d->dlg == 1339 && pending.p.type == CAM_RADIAL) || (d->dlg == 1328 && pending.p.type == CAM_SCALLOP)) && d->id == 1239) {   /* UpdateLocalOp FUN_180088bd0 (Spiral / Radial: FUN_180098a50 -> it) */
        int i = (int)v; pf_put(0x16d4, PFT_U8, i == 3 ? 1 : i == 4 ? 2 : 0); pf_put(0x16bd, PFT_U8, i == 2); pf_put(0x16d5, PFT_U8, i == 1); return; }
    if (d->dlg == 1327 && pending.p.type == CAM_HORIZAREA) {   /* UpdateLocalOp FUN_18008c460 ms l.91843-91869 (0x16e4 ms l.91554-91558; 0x1558 = n - 1 ms l.91851) */
        if (d->id == 1239) pf_put(0x16e4, PFT_U8, (int)v == 1);
        if (d->id == 1302) pf_put(0x1558, PFT_S32, (int)v - 1 < 0 ? 0 : (int)v - 1);
        return; }
    if (d->dlg == 1326 && pending.p.type == CAM_AREAROUGH) {   /* UpdateLocalOp FUN_180087880 l.88526-88549 / FUN_18009dc20 */
        if (d->id == 1239) pf_put(0x16e4, PFT_U8, (int)v == 1);
        if (d->id == 1209) { pf_put(0x16c0, PFT_U8, v > 0); pf_put(0x16c1, PFT_U8, v > 1); }
        return; }
    if (d->dlg == 1324 && pending.p.type == CAM_WATERLINE && d->id == 1239) {   /* UpdateLocalOp FUN_1800a4230: 0x16bd = Zigzag, 0x16be = Climb */
        pf_put(0x16bd, PFT_U8, (int)v == 2); pf_put(0x16be, PFT_U8, (int)v == 0); return; }
    if (d->dlg == 6546) { int pos, k = cool_k(d->id, &pos); if (k < 0) return;   /* batch 11: OnSelchange*OOICmb / OnSelchangeBWACmb -> UpdateCannedText */
        PvCool c; cool_get(&c); if (pos) c.pos[k] = (int16_t)v; else c.state[k] = (int16_t)v; cool_put(&c); return; }
    if (d->dlg == 3005) { int h = d->id == 3009, r = d->id == 3008;   /* the store FUN_18017d020 */
        pf_put(0x12a0, PFT_U8, h || r); pf_put(0x12a2, PFT_S16, h ? 0 : r ? 1 : 2); }
    if (d->dlg == 2823 && d->id == 3405) { int i = (int)v; pf_put(0x12c0, PFT_U8, i == 1); pf_put(0x12c1, PFT_U8, i == 2); }
    if (d->id == 3328 && (d->dlg == 3205 || d->dlg == 2784)) {   /* batch 9: UpdateLocalOp FUN_180167a40 (l.251842): style 2 -> 0x5d2 and 0x126e =
                                                                    (index != 0); else index 2 Zigzag -> 0x1564 = 0, 0x1784 |= 2; index 0 / 1 ->
                                                                    0x1564 = (index == 1), 0x1784 &= ~2 */
        int i = (int)v;
        if (pf_style() == 2) { pf_put(0x5d2, PFT_U8, i != 0); pf_put(0x126e, PFT_U8, i != 0); }
        else { int b = (int)pf_rv(0x1784, PFT_S32); pf_put(0x1564, PFT_U8, i == 1); pf_put(0x1784, PFT_S32, i == 2 ? (b | 2) : (b & ~2)); }
    }
}
static int pf_custom_items(const PfDef *d, const char **it, int *code) {
    int n = 0;
    if (d->dlg == 2823 && d->id == 3405) {   /* mcmillres 378 / 379 / 380 */
        static const char *s[3] = { "Helix upwardly", "Helix downwardly", "Circle" }; for (int i = 0; i < 3; i++) { it[n] = s[i]; code[n++] = i; } }
    if (d->id == 3328 && (d->dlg == 3205 || d->dlg == 2784)) {
        if (pf_style() == 2) { it[n] = "Left"; code[n++] = 0; it[n] = "Right"; code[n++] = 1; }   /* mcmillres 0x4ed / 0x4ef */
        else { it[n] = "Climb"; code[n++] = 0; it[n] = "Conventional"; code[n++] = 1; it[n] = "Zigzag"; code[n++] = 2; }   /* 0xa61 / 0xa62 / 0x169 */
    }
    if (d->dlg == 6546) {   /* batch 11: SetDialogData - state: On (itemdata 0, CoolantOnTxt) when CoolantOnCmd, Off (1, CoolantOffTxt) when
                               CoolantOffCmd, Ignore (2, mccoreres 7503) unless V9 style or the option is disabled; position: Before / With / After
                               (mccoreres 7504-7506) for an enabled option, X style only */
        int pos, k = cool_k(d->id, &pos), fp = pv_cool_frompost(PV_MACH());
        if (k >= 0 && !pos) {
            if (pv_cool_on_cmd(PV_MACH(), k, NULL, 0)) { it[n] = PV_MACH()->mach_cool_on[k]; code[n++] = 0; }
            if (pv_cool_off_cmd(PV_MACH(), k, NULL, 0)) { it[n] = PV_MACH()->mach_cool_off[k]; code[n++] = 1; }
            if (!fp && pv_cool_enabled(PV_MACH(), k)) { it[n] = "Ignore"; code[n++] = 2; }
        }
        if (k >= 0 && pos && !fp && pv_cool_enabled(PV_MACH(), k)) { static const char *b[3] = { "Before", "With", "After" }; for (int i = 0; i < 3; i++) { it[n] = b[i]; code[n++] = i; } }
    }
    if (d->dlg == 6548 && d->id >= 3162 && d->id <= 3165) {   /* batch 11: CannedTextPageBase lists (cam_postvals.c pv_canned_split / label) */
        static char lb[4][PF_MAXITEMS][64];
        if (d->id == 3162) { for (int i = 1; i <= 10 && n < PF_MAXITEMS; i++) { snprintf(lb[0][n], 64, "%d  %s", i, pv_canned_text(i)); it[n] = lb[0][n]; code[n] = i; n++; } }
        else { int16_t can[20], e[20]; cool_arr(can); int ne = pv_canned_split(can, d->id - 3163, e);
            for (int i = 0; i < ne && n < PF_MAXITEMS; i++) { pv_canned_label(PV_MACH(), e[i], lb[d->id - 3162][n], 64); it[n] = lb[d->id - 3162][n]; code[n] = e[i]; n++; } }
    }
    if ((d->dlg == 1517 || d->dlg == 1037 || d->dlg == 1002) && d->id == 997) {   /* HST Project: the source op (Mastercam: the op tree COpMgrTree; ours: a list of the part's ops, "(none)" = 0) */
        static char lb[PF_MAXITEMS][64]; it[n] = "(none)"; code[n++] = 0;
        for (int i = 0; i < F.nops && n < PF_MAXITEMS; i++) { if (pending.p.file_op_id > 0 && F.ops[i].id == pending.p.file_op_id) continue;   /* not itself */
            snprintf(lb[n], 64, "%d - %s%s%s", F.ops[i].id, F.ops[i].type, F.ops[i].comment[0] ? " " : "", F.ops[i].comment); it[n] = lb[n]; code[n] = F.ops[i].id; n++; }
    }
    if (d->dlg == 1337 && d->id == 25618) {   /* mcmsurfres 25543-25545 */
        static const char *s[3] = { "Toolpath group only", "Machine group only", "All groups" }; for (int i = 0; i < 3; i++) { it[n] = s[i]; code[n++] = i; } }
    return n;
}

/* ---- shown / hidden (the pages' ShowWindow calls) */
static int pf_hidden(const PfDef *d) {
    const CamParams *p = &pending.p; int t = p->type, st = pf_style(), id = d->id;
    if (d->st == PFS_H) return 1;
    if (d->dlg == 6555 && id >= 3449 && id <= 3486) return 1;   /* batch 11: MiscellaneousValuesPageBase::HideCheckBoxes 18052dee0 (0xd79-0xd9e) */
    switch (d->dlg) {
    case 25592:   /* HSTPtBasedCutPage::InitPage (mcmill_all.c l.270944): circle mill hides 0x670 / 0x6ca / 0x59d / 0x674 and the
                     labels 0x564 / 0x568 (on page 3271); 0x3a4 / 0x3a1 shown only for opcode 0x71 */
        if (t == CAM_CIRCLE && (id == 1648 || id == 1738 || id == 1437 || id == 1652 || id == 1380 || id == 1384)) return 1;
        return id == 932 || id == 929;
    case 25598: case 3030: case 25602:   /* MapToDlg ShowWindow 0 for opcodes 0x12 / 0x66 / 0x69 / 0x71 (taper, depth cut order) */
        if ((t == CAM_CIRCLE || t == CAM_FACE || t == CAM_SLOT) && (id == 1381 || id == 1979 || id == 1980 || id == 3230)) return 1;
        if (d->dlg == 25602) {   /* FUN_180147e30: 1377 hidden for opcode 0x86; 1979 / 1980 / 3230 hidden for 0x86; island controls
                                    shown only for styles 0 / 1 */
            if (id == 1377 || id == 1979 || id == 1980 || id == 3230) return 1;
            if ((id == 1403 || id == 1466 || id == 3013 || id == 3304 || id == 25199 || id == 25196) && st > 1) return 1;
        }
        return 0;
    case 3013: case 2793:   /* FUN_180171320: midpoint / gouge check shown for opcode 1, 0x85 and 0x86 style 2; FUN_180159770: the
                               overlap hidden for 0x86 styles 3 / 4 */
        if ((id == 25158 || id == 25200) && !(t == CAM_CONTOUR || st == 2)) return 1;
        return id == 25324 && st == 3;
    case 2826: return id == 1011 || id == 2891;     /* MapToDlg ShowWindow 0 on 0x3f3 and the label 0xb4b */
    case 1343: return id == 704;                    /* mcmill MapToDlg ShowWindow(0x2c0, 0) */
    case 3205:   /* FUN_180168b70: 3438 hidden for style 2; FUN_180168c30 (l.252613-633): 3356 shown only for opcode 0x86 with record
                    byte 0x1228 & 0xfd == 0 (styles 0 / 2) */
        return (id == 3438 && st == 2) || (id == 3356 && !(pf_opcode() == 0x86 && (st == 0 || st == 2)));
    /* textfit 2026-11-17 (UI-5, Mastercam's ShowWindow calls): template controls without WS_VISIBLE that the pages never show, or
       show by state - they were drawn on top of their neighbours (the Mastercam-default rule) */
    case 999:    /* CMsRPocPg (mcmsurfres): init FUN_1800dc360 l.143645-653 / check 1175 clicked FUN_1800de5e0 l.144526-546: check 1175
                    (record 0x12b8) on -> 1111 "Total offset" shown, 1108 "Additional offset" hidden; off -> the reverse */
        if (id == 1111) return pf_rv(0x12b8, PFT_U8) == 0;
        if (id == 1108) return pf_rv(0x12b8, PFT_U8) != 0;
        return 0;
    case 242: case 1312:   /* CMsPocPg / CMsPocPgLev2: 1181 "Last rough" replaces 720 "Finish" only with tp.cfg question 0x1c
                              (msurf_use_new_pockhs_ctrls, set at l.18592; Mastercam's default: off) - FUN_1800f08a0 l.154917-948,
                              FUN_1800efb10 l.154412-424; 1183 / 1185 never referenced; Lev2 (FUN_1800f1170 l.155011-017) shows only
                              1173 / 1174 / 720 of its hidden ones */
        if (id == 1181 || id == 1183 || id == 1185) return 1;
        if (d->dlg == 1312) switch (id) { case 719: case 721: case 724: case 725: case 726: case 728: case 749: case 750: case 984:
                                           case 1029: case 1344: case 1345: case 25256: return 1; }
        return 0;
    case 240: return id == 519 || id == 523;        /* CMsPnclPg init FUN_1800ebaa0 l.152310-330: 634 / 1188 shown, 519 / 523 never */
    case 1802: return id == 1075 || id == 1104;     /* engrave.dll: no ShowWindow of them (DDX only, 0x1800097bc) */
    case 1803: return id == 1036 || id == 1048 || id == 1079;   /* engrave.dll: DDX / EnableWindow only (0x18000d7bb-80b, 0x18000e169) */
    case 2938: case 2940: case 3005: case 3025:     /* the invisible frame 2990 the child panels are placed in (mcmill_all.c l.240962,
                                                       264365, 265466): never shown */
        return id == 2990;
    case 3219: {  /* FUN_180169600: by the entry method 0x16bb */
        int m = (int)pf_rv(0x16bb, PFT_U8);
        return (id == 3210 && m != 2) || (id == 3211 && m != 4 && m != 5) || (id == 3226 && m != 0); }
    case 2996:    /* the chamfer child's Depth 25140 / label 1798 overlap 1702 in the template: shown by a handler that is
                     NOT DECODED (ours: hidden) */
        return id == 25140 || id == 1798;
    }
    return 0;
}

/* ---- batch 6: HSTPocketRoughPage 3003 enables (decomp/mcmill_all.c): FUN_1801837b0 l.269514 (Rough 1360 on: 25329 / 1364 /
   1366 / 1362 / 1368; the list 1419 also needs pocket type != 3 Remachining (FUN_180184e10) and not Open pocket with record 0x13f0
   (FUN_180184df0: op +0x3482 == 4 && +0x3478)), FUN_180183260 l.269300.. by the selected method: 1362 Spiral inside to outside
   unless Zigzag / One Way, 1366 Roughing angle only for Zigzag / One Way, 1368 Minimize tool burial for methods 0-3;
   FUN_1801836b0: the trochoidal radios 2170-2172 only for High Speed (8), corner smoothing 2176 for High Speed with the radio
   != 2 (Entire pocket), loop radius / spacing 2173 / 2174 for High Speed with the radio != 0. 0 = greyed */
static int pf_pock_rule(const PfDef *d) {
    const CamParams *p = &pending.p;
    if (d->dlg != 3003 || p->type != CAM_POCKET) return 1;
    int rough = !p->pocket_no_rough, m = p->pk_ok ? (int)pf_rv(0x123c, PFT_S16) : 8, tro = (int)pf_rv(0x1408, PFT_S16);
    int ty = (int)pf_rv(0x13fa, PFT_S16), open = ty == 4 && pf_rv(0x13f0, PFT_U8) != 0;
    if (!rough) return d->id == 1360;
    switch (d->id) {
    case 1419: return ty != 3 && !open;
    case 1362: return !open && m != 0 && m != 6;
    case 1366: return !open && (m == 0 || m == 6);
    case 1368: return !open && m >= 0 && m <= 3;
    case 2170: case 2171: case 2172: return !open && m == 8;
    case 2176: return !open && m == 8 && tro != 2;
    case 2173: case 2174: return !open && m == 8 && tro != 0;
    }
    return 1;
}

/* ---- decoded fields whose effect on the toolpath is NOT DECODED here (greyed + the marker; the reason as the tooltip) */
static const char *pf_engine_nd(const PfDef *d) {
    int t = pending.p.type;
    if (d->dlg == 6554) switch (d->id) {   /* batch 11 */
    case 3674: case 3675: case 3676:
        return "Home position (op +0x870): Mastercam passes it into the NCI tool change (1002) through FUN_1800a0d70 (dependent operations, "
               "the op's view) - NOT DECODED, shown only; the LinuxCNC post returns with G91 G28 Z0. / G28 X0. Y0. as the user's post";
    case 3663: case 3665: case 1926: case 1932: case 1941: case 1929: case 1936: case 1945: case 2483: case 2487: case 2491: case 2495:
    case 2499: case 2504: case 2696: case 2698: case 3447: case 2703:
        return "Reference points (approach op +0x150 / +0x158, retract +0x292 / +0x298, flags +0x2b0): the moves they make (mcmill rapid_approach) "
               "are NOT DECODED - shown only; every operation of the user's parts has them off";
    }
    if (d->dlg == 6548 && d->id >= 3162 && d->id <= 3165)
        return "N:display only - adding / removing canned text (CannedTextPageBase::OnAddBeforePb / OnAddWithPb / OnAddAfterPb / OnRemovePb) is "
               "not ported; the LinuxCNC post writes no canned text (the Mastercam post reads it from the NCI 1025)";
    if (d->dlg == 10318) switch (d->id) {   /* batch 10 */
    case 10374: case 10375:
        if (!cam_arc_filter_planes_on())
            return "N:XZ (G18) / YZ (G19) arcs (0x33c / 0x33d): decoded (FitArc FUN_18093ab00 views 2 / 5), posted as G18 / G19 - only with "
                   "MCAM_ARC_FILTER_PLANES=1 (formats/post.md \"Arc filter\"); without it the filter makes XY arcs only";
        break;
    case 10386: case 10382: case 10397: case 10401: case 10398: case 10399: case 10400:
        if (t != CAM_CONTOUR)   /* 2026-11-08: ported for Contour (cam_smooth.c, formats/contour_smoothing.md) */
            return "Smoothing (0xca8..0xcac, 0xc98, 0xca0): BNCISection::PostProcess -> bnci_point_distribution (cam_smooth.c) is ported for Contour "
                   "only - the records between this operation's moves, where its point list is flushed (FUN_180945d50), are NOT DECODED here";
        break;
    }
    /* batch 7 greyed 3020 1847 / 1724 here (NOT PORTED); decisions batch dec1 (B7-3) ported the exact-arc ramp (cam2d_slot_arc.c) */
    if (t == CAM_OPTIROUGH) switch (K(d->dlg, d->id)) {   /* 3drough: what cam3d_rough.c does not take (formats/hst3d_port_plan.md 6) */
    case K(1465, 1480): case K(1465, 1469): case K(1465, 1470): case K(1465, 1481):
        return "Stepup (0x16ec / 0x1478, StepUpEnable / StepUp): the flat-level step-ups (FUN_180411bd0 / FUN_180414620) and their region order "
               "are not ported - an op with it on is refused";
    case K(1465, 1209): case K(1465, 1486): case K(1465, 1489):
        return "Optimize cut order / stepups / stepdowns (0x16c0 / 0x172d / 0x1660): the executioner's region order (mckernel FUN_1801068c0) "
               "is NOT DECODED - regions are cut level by level in the slice's order";
    case K(1465, 692):
        return "Tip compensation (0x2f8): its combo items and the Center case are NOT DECODED - the levels are tool-tip heights";
    case K(1430, 668):
        return "Skip pockets smaller than (0x13e8): the region filter (\"Removing over offset pocket\") is NOT DECODED - no pocket is skipped";
    case K(1430, 1446): case K(1430, 1431): case K(1430, 1439): case K(1430, 1440):
        return "Entry feeds / speeds (0x16ea -> CDynPockParm flag 0x40000000, ramp feed 0x1468): the air dwell it adds (FUN_180071df0) is "
               "NOT DECODED (as Dynamic Mill); S words stay greyed until an F / S override opt-in exists";
    }
    /* batch 9: 2D High Speed */
    if (t == CAM_DYNAMIC) switch (K(d->dlg, d->id)) {
    case K(3219, 3226):
        return "Center helix on point (0x16be -> CDynPockParm flag 0x100000): the source of the entry points it centres on is NOT DECODED "
               "(mcce_dyn.c plunge_entry: the helix goes to the max-clearance point)";
    case K(3219, 3219):
        return "Entry feeds / speeds (0x16bc -> flag 0x40000000 and the ramp feed 0x1668): the air dwell it adds after the entry (FUN_180071df0, "
               "radius search FUN_180072280) is NOT DECODED";
    case K(3219, 986):
        return "N:Ramp feed rate (0x1668, used with 0x16bc): an F word of the entry - greyed with its group (air dwell NOT DECODED) and until an "
               "F / S override opt-in exists";
    case K(3219, 3212): case K(3219, 3213):
        return "N:Ramp / dwell spindle speed (0x1698 / 0x1670): S words inside the op - greyed until an F / S override opt-in exists";
    case K(3219, 3208): case K(3219, 3209):
        return "Entry chains (Custom entry, flag 0x8000): FUN_180074560 and the chain cleanup FUN_180132200 are NOT DECODED here";
    }
    if (t == CAM_AREA) switch (K(d->dlg, d->id)) {
    case K(379, 2346): case K(379, 2348): case K(379, 2350): case K(379, 2352):
        return "Keep tool down within (0x15cd / 0x1600 / 0x1608 = McHmm LinkMaxStayOnSurface): McHmm's linker (EvaGeneralLinkerPlan "
               "FUN_1803a0c50) is NOT DECODED - this program's Area Clearance links stay down only inside the tool-centre region, else "
               "retract (cam2d_area.c, formats/area_mill.md 11.5)";
    case K(379, 2211): case K(379, 313):
        return "Max radius / Profile tolerance (0x15a0 / 0x15a8 = ProfileSmoothingMaxRadius / CornerTolerance): McHmm's profile smoothing "
               "(FUN_180557f60) is NOT DECODED; the ported passes take the corner offset tolerance only (formats/area_mill.md 11.7)";
    case K(2826, 25173): case K(2826, 25164): case K(2826, 1004):
        return "Entry helix (0x15ca = 1, radius 0x15d0): this program's Area Clearance enters by the profile ramp only; McHmm's helix entry is "
               "NOT DECODED (formats/area_mill.md 11.4)";
    case K(2826, 1005):
        return "Z clearance (0x15d8 -> RampHeightOffset): the ramp's Z spreading is NOT DECODED - the ramp starts 0.1 above the level "
               "(measured, cam2d_area.c)";
    case K(2826, 1008):
        return "Skip pockets smaller than (0x15f8 = MinProfileDiameter): its use in McHmm's linking plan is NOT DECODED";
    }
    if (t != CAM_CONTOUR && t != CAM_POCKET) return NULL;
    switch (K(d->dlg, d->id)) {
    case K(25598, 1463): case K(25598, 1643): case K(25598, 1644):
        if (t != CAM_CONTOUR) return NULL;
        return "Subprogram (0x890) / Absolute - Incremental (0x8a3): the contour writes NCI 1018 / 1019 around each pass (subpgm_set, "
               "set_n1018 with op +0x83b, bnci_wr_1018 / 1019 - decomp/mcmill_ctour.c l.933-934 / 8958-8960 / 9742-9744; mccore decompiled "
               "2026-11-07); the depth copies' sub-program form (FUN_180045770) and the post's subprogram output (Generic Fanuc 3X Mill.pst) "
               "are NOT DECODED";
    case K(2793, 25159): case K(2793, 25179):
        return "First / last depth cut only (0x928 / 0x970): ported for Contour (cam2d.c gen_contour lead_skip, the contour loop's "
               "lead flags read from the disassembly); the pocket finish leads (FUN_1801cd200) are NOT DECODED";
    case K(2793, 25333): case K(2793, 25375):
        return "Plunge after first move / retract before last move (0x992 / 0x993): written as NCI markers by the depth copies "
               "(FUN_180045770, decomp/mcmill_ctour.c l.6139); the markers' motion is NOT DECODED";
    /* K(3030, 1377) Keep tool down (0x891) and K(3004, 1377) Keep tool down (0x126a): ported 2026-11-12 (wr_pock_toolpath's
       keep-tool-down markers, cam2d_pock.c pk_ktd_*; formats/pocket_engine_b6.md "Keep tool down 0x126a"); Remachining refuses 0x126a */
    /* batch 5 */
    /* batch 6 (pocket) */
    case K(3004, 1394):
        return "Spring passes (0x1420): the passes are decoded (FUN_1801ca670), the link between their sections (wr_pock_toolpath) is "
               "NOT DECODED; an op with spring passes is refused";
    case K(2793, 25369): case K(2793, 25219): case K(2793, 25370): case K(2793, 25220):
        return "Lead ramp / helix height (0x908 / 0x920 / 0x950 / 0x968): the switch at the end of FUN_1801bdc00 is decoded, the "
               "raised plunge's moves are NOT DECODED; a pocket with a height is refused";
    case K(2793, 25169): case K(2793, 25183): case K(2793, 25170): case K(2793, 25184):
        return "Use entry / exit point (0x929 / 0x971): the point lookup FUN_1801c38a0 / FUN_1801c2ab0 is NOT DECODED for the pocket "
               "finish; a pocket with it on is refused";
    case K(3004, 2181): case K(3004, 2183): case K(3004, 1407): case K(3004, 1415):
        return "Finish feed / spindle override (0x1434 / 0x1428, 0x1435 / 0x1430): changes the F / S words of the posted program - "
               "greyed until an opt-in for F / S overrides is designed (as the Multi Passes and lead feed overrides)";
    case K(2940, 1423):
        return "Create additional finish operation (0x123b, HSTPocketCutPage store FUN_18017b760): the creation of the additional "
               "operation is NOT DECODED";
    case K(3010, 1534): case K(3011, 2002):
        return "Save skipped boundary (0x12e0 / 0x134b): its use by the entry code is NOT DECODED";
    case K(3003, 1470):
        return "Display stock for constant overlap spiral (0x1281): its use by the engine is NOT DECODED";
    }
    return NULL;
}

/* ---- pictures of the template statics (the existing pp_page_pic / pp_stock_bmp logic, by static id) */
static int pf_pic(const PfDef *d, int ox, int oy, char *file, size_t fn) {
    const CamParams *p = &pending.p; int t = p->type, x, y, w, h;
    switch (d->id) {
    case 2908:   /* HST2DCutParametersPageBase::SetStockToLeaveBmp 0x180147720 (walls, floors) */
        snprintf(file, fn, "mcmillres_BMPK_%d.png", pp_stock_bmp(p->stock_xy, pp_hf.fstock)); return 1;
    case 1358:
        if (d->dlg == 2938 || d->dlg == 3025) { snprintf(file, fn, "mcmillres_BMP_%d.png", p->side == SIDE_ON ? 188 : p->side == 2 ? 181 : 178); return 1; }   /* FUN_180158db0; 3025: FUN_18015e000 (0xbc Off / 0xb2 Left / 0xb5 Right) */
        if (d->dlg == 2940) { snprintf(file, fn, "mcmillres_BMP_%d.png", pf_rv(0x123a, PFT_U8) ? 25742 : 25743); return 1; }   /* FUN_18017bf60 */
        return 0;
    case 1360: if (d->dlg != 2942) return 0;   /* FUN_18016ba50 / FUN_18016caf0 by the style (op +0x11c0) */
        snprintf(file, fn, "mcmillres_BMP_%d.png", p->face_style == 0 ? 25753 : p->face_style == 3 ? 25755 : p->face_style == 5 ? 25756 : 25754); return 1;
    }
    /* the page pictures of pp_page_pic: at the static with the same rectangle */
    if (pp_page_pic(p, pp_page_shown(), &x, &y, &w, &h, file, fn) && x == d->x + ox && y == d->y + oy) return 1;
    (void)t; return 0;
}

/* ---- ours: controls of this program kept on a template page (the generators' inputs Mastercam's page has not) */
enum { PFO_TIPDIA = 90001, PFO_CHPREV, PFO_HOLES, PFO_HPITCH, PFO_THDEPTH, PFO_THFULL, PFO_REGION, PFO_PLUNGE, PFO_WALL,
       PFO_DCSIDE, PFO_GAP, PFO_ENTRYR, PFO_ARCS, PFO_NOTPOSTED, PFO_BOX, PFO_MINRAD, PFO_MICRO, PFO_BACKF };
typedef struct { PfShown *o; int cap, n; } PfCtx;
static PfShown *pf_add(PfCtx *c, int kind, int id, int x, int y, int w, int h, const char *text) {
    if (c->n >= c->cap) return NULL;
    PfShown *s = &c->o[c->n++]; memset(s, 0, sizeof *s);
    s->kind = kind; s->id = id; s->x = x; s->y = y; s->w = w; s->h = h; s->live = 1; s->cur = -1;
    snprintf(s->text, sizeof s->text, "%s", text ? text : "");
    return s;
}
static void pf_num(char *o, size_t n, double v, int type) { if (type == PFT_S16 || type == PFT_S32 || type == PFT_U8) snprintf(o, n, "%d", (int)v); else snprintf(o, n, "%.6g", v); }
static void pf_ours_edit(PfCtx *c, int id, int x, int y, int lw, const char *label, double v) {
    PfShown *s = pf_add(c, PFK_STATIC, -1, x, y + 2, lw, 10, label); if (s) s->ours = 1;
    s = pf_add(c, PFK_EDIT, id, x + lw + 2, y, 40, 14, ""); if (s) { s->ours = 1; pf_num(s->text, sizeof s->text, v, PFT_F64); }
}
static void pf_ours(PfCtx *c, int dlg, int ox, int oy) {
    CamParams *p = &pending.p; int t = p->type; PfShown *s;
    char l[160];
    switch (dlg) {
    case 2996:   /* 2D chamfer: the tool tip diameter and the preview (ours; was the row-laid Cut page) */
        pf_ours_edit(c, PFO_TIPDIA, ox + 7, oy + 106, 116, "Tool tip diameter:", p->tip_dia);
        { double re, zt; char err[160];
          if (chamfer_preview(p, &re, &zt, err, sizeof err)) { snprintf(l, sizeof l, "tip at Z%.3f, centre %.3f from the chain", zt, re); s = pf_add(c, PFK_STATIC, PFO_CHPREV, ox + 7, oy + 124, 180, 10, l); if (s) s->ours = 1; }
          else { s = pf_add(c, PFK_STATIC, PFO_CHPREV, ox + 7, oy + 124, 180, 20, err); if (s) { s->ours = 1; s->nd = 2; } } }   /* nd 2: the error colour */
        break;
    case 2942:   /* Facing without chains: the machine group stock's rectangle (make_facing_pock_bound) */
        if (p->use_box) { snprintf(l, sizeof l, "stock box %.2f x %.2f", p->box[2] - p->box[0], p->box[3] - p->box[1]); s = pf_add(c, PFK_STATIC, PFO_BOX, 24, 272, 200, 10, l); if (s) s->ours = 1; }
        break;
    case 25595:
        if (!(p->drill_cycle >= 0 && p->drill_cycle <= pf_drill_maxcyc())) { s = pf_add(c, PFK_STATIC, PFO_NOTPOSTED, 53, 148, 195, 10, "(not posted)"); if (s) s->ours = 1; }   /* batch 8: 3 / 4 with MCAM_DRILL_CYCLES=1 */
        break;
    case 25592: case 2824: {   /* the picked circles (and mcmillres 2397 "0 points defined!", tp_pt_mgr warning) */
        int n = 0; double dmin = 1e300, dmax = 0;
        for (int i = 0; i < pending.nholes; i++) if (pending.holes[i].d > 0) { n++; dmin = fmin(dmin, pending.holes[i].d); dmax = fmax(dmax, pending.holes[i].d); }
        if (n && dmax > dmin) snprintf(l, sizeof l, "%d circle(s), diameter %.3f - %.3f", n, dmin, dmax);
        else if (n) snprintf(l, sizeof l, "%d circle(s), diameter %.3f", n, dmin);
        else snprintf(l, sizeof l, "0 points defined!");
        if (dlg == 25592) { s = pf_add(c, PFK_STATIC, PFO_HOLES, 20, 130, 250, 10, l); if (s) s->ours = 1; }
        else {
            s = pf_add(c, PFK_STATIC, PFO_HOLES, 160, 262, 270, 10, l); if (s) s->ours = 1;
            /* threadmill: the "Thread depth" / "Full-profile tool" controls (ours) are gone - gen_thread never read them */
        }
        break; }
    case 3205: {
        int y = 246;
        if (t == CAM_DYNAMIC) {   /* Machining region strategy (dialog 657, op +0x1567: 1 = From outside); not the 3328 control */
            s = pf_add(c, PFK_STATIC, -1, 201, y + 2, 90, 10, "Machining region strategy"); if (s) s->ours = 1;
            s = pf_add(c, PFK_COMBO, PFO_REGION, 294, y, 110, 30, ""); if (s) { s->ours = 1; s->nitems = 2; s->items[0] = "Stay inside"; s->items[1] = "From outside"; s->cur = p->dyn_outside ? 1 : 0; snprintf(s->text, sizeof s->text, "%s", s->items[s->cur]); }
        } else pf_ours_edit(c, PFO_PLUNGE, 201, y, 91, "Plunge angle", p->ramp_deg);
        /* batch 9: the Dynamic Contour stand-ins "Wall stock width" / "Outside / Inside the chain" are removed - the generator
           (gen_dyncontour_mcce) never read them; it takes 3328 Left / Right (0x5d2) and page 3299 from the record */
        break; }
    /* batch 9: removed - 379's "Min toolpath radius / Micro lift / Back feedrate / Plunge angle", 2784's "Plunge angle / Gap size" and
       3219's "Helix radius (0 = auto)": no generator read them (Area Mill: Area Clearance or refused; Peel Mill: not generated; the
       Dynamic entry takes record 0x16c0, control 1004) */
    case 10318:   /* this program's arc output switch (CamParams.arcs: cam2d.c arc fitting, nc_post.c G2 / G3), not record 0x33b;
                     the old mcmillres 318 label */
        s = pf_add(c, PFK_CHECK, PFO_ARCS, 255, 278, 150, 10, "Create arcs in XY"); if (s) { s->ours = 1; s->on = p->arcs != 0; }
        break;
    }
}
static void pf_ours_set(int id, double v) {
    CamParams *p = &pending.p;
    switch (id) {
    case PFO_TIPDIA: p->tip_dia = pf_clamp(v, 0, 50); break;
    case PFO_THDEPTH: p->thread_depth = pf_clamp(v, 0, 20); break;
    case PFO_THFULL: p->thread_full = v != 0; break;
    case PFO_REGION: p->dyn_outside = v != 0; break;
    case PFO_PLUNGE: p->ramp_deg = pf_clamp(v, 0.5, 30); break;
    case PFO_WALL: p->wall_stock = pf_clamp(v, 0.1, 200); break;
    case PFO_DCSIDE: p->side = v ? SIDE_INSIDE : SIDE_OUTSIDE; break;
    case PFO_GAP: p->link_retract = pf_clamp(v, 0, 1000); break;
    case PFO_ENTRYR: p->entry_r = pf_clamp(v, 0, 100); break;
    case PFO_ARCS: p->arcs = v != 0; break;
    case PFO_MINRAD: p->min_rad_pct = pf_clamp(v, 0, 100); break;
    case PFO_MICRO: p->micro_lift = pf_clamp(v, 0, 5); break;
    case PFO_BACKF: p->back_feed = pf_clamp(v, 0, 50000); break;
    }
}

/* ---- "(ours)" markers: a control under Mastercam's label that this program fills differently (the user's choice
   2026-10-22: keep today's behaviour, mark it); a static beside the control with the explanation as its tooltip */
static void pf_mark(PfCtx *c, int x, int y, const char *tip) {
    PfShown *s = pf_add(c, PFK_STATIC, 91000 + c->n, x, y, 24, 10, "(ours)"); if (!s) return;
    s->ours = 1; snprintf(s->tip, sizeof s->tip, "%s", tip);
}
static void pf_marks(PfCtx *c, int dlg) {
    const CamParams *p = &pending.p; int t = p->type;
    static const char *COMP = "Compensation type: this program offers Computer / Off only - the LinuxCNC post writes no G41 / G42 "
                              "(mccoreres 4589 / 4597; Control / Wear / Reverse Wear not offered)";
    switch (dlg) {
    case 2938: case 3025:
        if (p->contour_type != 2 && !pf_cc_ok()) pf_mark(c, 188, 22, COMP);
        else if (p->contour_type != 2)
            pf_mark(c, 188, 22, "Compensation (MCAM_CUTTER_COMP=1, ours): Control posts G41 / G42 D<tool number> after the plunge, G40 on "
                    "the pass's last move (FUN_180056be0). Wear / Reverse Wear: refused by the LinuxCNC post");
        break;
    case 2944:
        if (p->mc_ok && !nc_cutter_comp_on() && (p->mc_comp == 0 || p->mc_comp == 4)) pf_mark(c, 188, 25, COMP);
        else if (p->mc_ok && nc_cutter_comp_on())   /* batch 7 */
            pf_mark(c, 188, 25, "Compensation (MCAM_CUTTER_COMP=1, ours): Control posts G41 / G42 D<tool number> on the finish passes' 2nd "
                    "record, G40 on their last (FUN_180218410). Wear / Reverse Wear: refused by the LinuxCNC post");
        break;
    case 3004:   /* batch 6: the pocket finish compensation 0x13a8 (cam2d_pock_fin.c) */
        if (t == CAM_POCKET && p->finish_n > 0) {
            if (!nc_cutter_comp_on()) pf_mark(c, 260, 41, "Cutter compensation: this program posts computer compensation only - Control / Wear / "
                                       "Reverse Wear need MCAM_CUTTER_COMP=1 (ours, formats/post.md); the LinuxCNC post writes no G41 / G42");
            else pf_mark(c, 260, 41, "Compensation (MCAM_CUTTER_COMP=1, ours): Control posts G41 / G42 D<tool number> on the finish pass's "
                         "first move, G40 on its last (FUN_1801cd200). Wear / Reverse Wear: refused by the LinuxCNC post");
        }
        break;
    case 25592: if (p->mc_ok && (p->mc_comp == 0 || p->mc_comp == 4)) pf_mark(c, 188, 17, COMP); break;
    case 2824:   /* threadmill */
        if (p->mc_ok && !nc_cutter_comp_on() && (p->mc_comp == 0 || p->mc_comp == 4)) pf_mark(c, 189, 100, COMP);
        else if (p->mc_ok && nc_cutter_comp_on())
            pf_mark(c, 189, 100, "Compensation (MCAM_CUTTER_COMP=1, ours): Control posts G41 / G42 D<tool number> on the last entry line, G40 on the "
                    "first exit line (FUN_180236810); without entry / exit lines on the arcs - refused by the LinuxCNC post. Wear / Reverse Wear: "
                    "refused by the LinuxCNC post");
        break;
    case 2942:   /* batch 7 */
        if (!cam_face_link_feed_on() && (int)pf_rv(0x1228, PFT_S16) == 0)
            pf_mark(c, 409, 206, "Feed rate between cuts (0x1298 / 0x12a0) changes the links' F word: honoured only with MCAM_FACE_LINK_FEED=1 "
                    "(ours, formats/post.md); without it the links take the feed rate, as with the box cleared");
        break;
    case 2784: case 3205:   /* decisions batch dec2 (Q2, ours): no Mastercam NCI of Peel Mill / Dynamic Contour exists to compare against */
        if (t == CAM_PEEL || t == CAM_DYNCONTOUR) {
            PfShown *s = pf_add(c, PFK_STATIC, 91000 + c->n, dlg == 2784 ? 211 : 201, dlg == 2784 ? 288 : 250, 120, 10, "Unverified (ours)");
            if (s) { s->ours = 1; snprintf(s->tip, sizeof s->tip, "%s: unverified - no Mastercam NCI of this style exists to compare against; checked only for gouges, wall stock "
                                          "and rapids (opdlg_port_audit.md H.3)",
                                          t == CAM_PEEL ? "Peel Mill" : "Dynamic Contour"); }
        }
        break;
    case 3013:
        if (t == CAM_CONTOUR && p->lead_on && p->lead_gouge)
            pf_mark(c, 232, 15, "Gouge check: Mastercam's check FUN_1801c46f0 is NOT DECODED - this program fits the leads instead "
                    "(full; else shorter lines; else the arcs 1 deg smaller at a time; else none - measured on the user's parts; the "
                    "two sides shrink together)");
        break;
    }
}

/* ---- the page's controls */
static int pf_group_on(int dlg, int gid) {   /* the state of group / check box GID on page DLG (enable rules "buddy of N") */
    for (int i = 0; i < (int)(sizeof PF_DEF / sizeof PF_DEF[0]); i++) {
        const PfDef *g = &PF_DEF[i]; if (g->dlg != dlg || g->id != gid) continue;
        double v; int live;
        if (pf_bget(g, &v, &live)) return v != 0;
        int off = pf_off(g); if (off < 0) return 0;
        double r = pf_rv(off, g->type);
        return g->val == PFV_INV ? r == 0 : g->val == PFV_EQ ? r == g->k : r != 0;
    }
    return 1;
}
/* batch 7: the decoded enables of the rows marked "another rule" (-1) on the Face Cut page 2942 (FUN_18016caf0 l.254623, on the
   style code: 5 Dynamic, 3 One pass, 0 Zigzag; 1857 = Zigzag only, dis 0x18016cd60..0x18016cd81) and 3020 1724 (FUN_18018e3a0
   l.276855: Ramp entry on and Output helixes as arcs off); other pages: not live (as before) */
static int pf_en_rule(const PfDef *d) {
    /* Pocket: Tapered walls (cam2d_pock_taper.c; Open pocket cutting method: cam2d_pock_open.c). HSTDepthCutsPage::enable_taper_controls (l.247981-248026) with the Depth cuts group's check (enable_depthcut_controls l.247974: +0x18e8): the check 1381 (0x565, +0x3a88) is the group's buddy (l.247447),
       the angle 1252 (0x4e4) needs the group and the check. The Subprogram check 1463 (0x5b7, +0x3b60: HWND +0x3ba0): checking taper with it warns 0x95b (mcmillres 2395) and unchecks; 1463 is not live for Pocket here */
    /* The island angle 1466 (0x5ba, +0x52c8) the same: FUN_18017c550's page rule, l.265140-265143, with the group's check; every Pocket type but Remachining (no taper port) */
    if (d->dlg == 3030 && (d->id == 1381 || d->id == 1252 || d->id == 1466) && pending.p.type == CAM_POCKET && pending.p.pk_ok && (int)pf_rv(0x13fa, PFT_S16) != 3) return pf_dc_on() && (d->id == 1381 || pf_rv(0x1288, PFT_U8) != 0);
    /* CMsPnclPg 240 (FUN_1800ec020 ms l.152417-152484): Number of passes 530 iff Parallel passes = Number per side; Stepover 532 / Cut method 833 iff not None */
    if (d->dlg == 240 && (d->id == 530 || d->id == 532 || d->id == 833)) { int k = (int)pf_rv(0x1324, PFT_S16); return d->id == 530 ? k == 1 : k != 0; }
    /* HSTContourRemachPage 2999: 1956 / 1958 iff the source radio = 2 (FUN_18015c3a0, decomp/mcmill_all.c) */
    if (d->dlg == 2999 && (d->id == 1956 || d->id == 1958)) return (int)pf_rv(0x124a, PFT_S16) == 2;
    /* HSTPocketRemachPage 3146: 1956 (0x7a4) / 1958 iff the source radio = 2 (FUN_180182dc0 l.269186-269188) */
    if (d->dlg == 3146 && (d->id == 1956 || d->id == 1958)) return (int)pf_rv(0x13b2, PFT_S16) == 2;
    /* Contour Tabs 2834 (FUN_180190d80 / FUN_180191050, decomp/mcmill_all.c): with the Tabs group on - Automatic: the Number /
       Max. distance radios, Number's edit iff by number, Max. distance's edit iff by distance, the shape radios, the shape size
       iff "Create tabs on shapes less than"; Manual: Position... (iff square point off), Use square point, Start / Midpoint / End */
    /* Contour Lead In/Out 3013: "Enter on first depth cut only" 25159 (0x6247) with the Entry group, "Exit on last depth cut only"
       25179 (0x625b) with the Exit group (FUN_18016fb00 / FUN_18016fcd0, decomp/mcmill_all.c l.256627 / l.256682) */
    /* Override feed rate: the check 25162 / 25177 with its group, the value 25163 / 25178 iff the check is on (FUN_18016fb00 l.256627: 0x624b
       with SendMessage BM_GETCHECK of 0x624a; FUN_18016fcd0 the exit side) */
    if (d->dlg == 3013 && pending.p.type == CAM_CONTOUR && (d->id == 25162 || d->id == 25177))
        return pending.p.lead_on && (d->id == 25162 ? pending.p.lead_in_on : pending.p.lead_out_on);
    if (d->dlg == 3013 && pending.p.type == CAM_CONTOUR && (d->id == 25163 || d->id == 25178))
        return pending.p.lead_on && (d->id == 25163 ? pending.p.lead_in_on && pf_rv(0x1358, PFT_U8) : pending.p.lead_out_on && pf_rv(0x1368, PFT_U8));
    /* Multi Passes feed override value 1407 iff its check 2181 (HSTMultiPassPage) */
    if (d->dlg == 25591 && d->id == 1407) return pending.p.type == CAM_CONTOUR && pf_rv(0x8a8, PFT_U8) && pf_rv(0x1464, PFT_U8);
    /* the spindle override value 1415 iff its check 2183 (OnSsOverrideCb 0x180178060; field table). Live since 2026-11-07: the swap of op
       +0x328 (FUN_18005b340 decomp/mcmill_ctour.c l.1744-1748 / 1843-1845, FUN_180045770 l.6968-6973 / 6193-6197) is read only by the
       writer FUN_180056be0 (l.8122-8127 -> iStack_12ae4), which writes it (bnci_wr_4, l.9779-9781) only for a chain point's own
       speed change (the record flag 0x20, l.8551-8554 / 8909-8912): a contour's finish passes get no spindle change (cam2d.c) */
    if (d->dlg == 25591 && d->id == 1415) return pending.p.type == CAM_CONTOUR && pf_rv(0x8a8, PFT_U8) && pf_rv(0x1465, PFT_U8);
    if (d->dlg == 3013 && (d->id == 25159 || d->id == 25179 || d->id == 25333 || d->id == 25375))   /* 25333 = 0x62f5 with the Entry group (FUN_18016fb00) */
        return pending.p.type == CAM_CONTOUR && pending.p.lead_on && (d->id == 25159 || d->id == 25333 ? pending.p.lead_in_on : pending.p.lead_out_on);
    if (d->dlg == 2834) {
        int on = pending.p.tabs > 0, aut = pf_rv(0x1381, PFT_U8) != 0;
        switch (d->id) {
        case 1103: case 1118: case 3194: case 3196: return on && aut;
        case 1121: return on && aut && pf_rv(0x1440, PFT_U8) != 0;
        case 1176: case 1179: return on && aut && pf_rv(0x13f0, PFT_U8) == 0;
        case 1114: case 1149: case 1151: case 1153: return on && !aut;
        case 1206: return on;   /* Partial: not in FUN_180190d80 - the Tabs group's enable, as its partner Full 1067 (en 1129) */
        }
        return 0;
    }
    /* batch 8: Thread Mill Cut 2824 FUN_180191ee0 (l.279068): 1674 Tolerance iff Linearize helixes 0x1282 or |taper 0x12a8| > 1e-6;
       Lead In/Out 2969 FUN_180192fe0 (l.279898): 1714 Start at center only for an ID thread (0x1290 = 0), 1670 line length iff OD
       or Start at center unchecked (inverted bool 0x12a1: checked = 0), 1711 Perpendicular entry off when ID, Start at center and
       arc clearance 0x1270 = 0 (the page then also clears it - not written here) */
    /* batch 8: Helix Bore 2823 FUN_18016e240 (l.255853): 1725 Finish pitch iff Finish 0x1278 and the method is not Circle (0x12c1),
       1740 Tolerance iff Output arc moves 0x12b0 off, 1749 Rough pass stepover iff rough passes 0x1258 > 1 */
    if (d->dlg == 2823) {
        if (d->id == 1725) return pending.p.finish_n > 0 && pf_rv(0x12c1, PFT_U8) == 0;
        if (d->id == 1740) return pf_rv(0x12b0, PFT_U8) == 0;
        if (d->id == 1749) return (int)pf_rv(0x1258, PFT_S16) > 1;
        return 0;
    }
    /* HST engines: Peel 2784 FUN_180179de0 (l.263199): 953 Approach distance / 3328 Cutting method enabled when 3330 Cutting style is
       "Dynamic peel" (0x17d8 = 0), 3438 Conventional feed also needs Zigzag (0x1784 bit 1); FUN_18017ab60 shows 631 / 633 / 634 /
       618 with a flag from the op's chain count == 1 (the flag variable is lost in the decompile, opdlg_fields.tsv PARTIAL):
       enabled for one chain - the case PreparePockBoundsPeel reads them (l.6630-6657) [inferred] */
    if (d->dlg == 2784 && pending.p.type == CAM_PEEL) {
        if (d->id == 953 || d->id == 3328) return pf_rv(0x17d8, PFT_U8) == 0;
        if (d->id == 3438) return pf_rv(0x17d8, PFT_U8) == 0 && ((int)pf_rv(0x1784, PFT_S32) & 2);
        if (d->id == 631 || d->id == 633 || d->id == 634 || d->id == 618) return pending.nchains == 1;
        return 0;
    }
    if (d->dlg == 1327) {   /* Horizontal Area 1327 (horizontal_area.md 5): Stepdown 1304 / 1303 only for more than one depth cut (FUN_18008bfe0 ms l.91639-91655) */
        if (d->id == 1304) return pf_rv(0x1558, PFT_S32) + 1 > 1;
        return 0; }
    if (d->dlg == 1329) {   /* Pencil 1329 (formats/pencil.md 4): 1250 / 706 with the Limit check (Ordinal_4282, ms l.97984-97985); Overlap /
                               Shallow angle for Down / Up Mill only (FUN_180087e00); Horizontal offset disabled by InitPage (ms l.97990-97991) */
        if (d->id == 1250 || d->id == 706) return pf_rv(0x16cb, PFT_U8) != 2;
        if (d->id == 1285 || d->id == 1286) return pf_rv(0x16d4, PFT_U8) != 0;
        return 0;
    }
    if (d->dlg == 6555) {   /* batch 11: EnableStuff 18052db40 - a value edit is enabled while 2252 is off and its post text entry enables it */
        int ix = misc_val_idx(d->id), has = 0; if (!ix) return 0;
        pv_misc_text(ix, &has); return pf_rv(PV_MISC_SET, PFT_U8) != 0 && has;
    }
    if (d->dlg == 2824) {
        if (d->id == 1674) return pf_rv(0x1282, PFT_U8) != 0 || fabs(pf_rv(0x12a8, PFT_F64)) > 1e-6;
        /* threadmill: FUN_180192050 (l.279150) sets this+0x61a8 when a point is not an arc / solid point picked at its centre (TP
           mode 0x43); FUN_180191ee0 enables 1412 / 1682 with it - the points whose diameter is rec 0x1298 (cam_fileop.c: Hole.d 0) */
        if (d->id == 1682) { for (int i = 0; i < pending.nholes; i++) if (!(pending.holes[i].d > 0)) return 1; return 0; }
        return 0;
    }
    /* threadmill Multi Passes 2825: FUN_180194910 (l.280977; the argument the decompile lost from dis 0x18019492c..0x18019495e):
       571 Feed Rate iff the group 2945 is checked and 586 (BM_GETCHECK of its hwnd this+0x80c8 = DDX 0x24a +0x8088 + 0x40),
       573 Spindle Speed iff 2945 and 588 (this+0x81a0) */
    if (d->dlg == 2825) {
        if (d->id == 571) return pf_group_on(2825, 2945) && pf_rv(0x12c4, PFT_U8) != 0;
        if (d->id == 573) return pf_group_on(2825, 2945) && pf_rv(0x12c5, PFT_U8) != 0;
        return 0;
    }
    if (d->dlg == 2969) {
        int id_th = (int)pf_rv(0x1290, PFT_S16) == 0, sac = pf_rv(0x12a1, PFT_U8) == 0;
        if (d->id == 1714) return id_th;
        if (d->id == 1670) return !id_th || !sac;
        if (d->id == 1711) return !(id_th && sac && pf_rv(0x1270, PFT_F64) == 0.0);
        return 0;
    }
    if (d->dlg == 3020) return d->id == 1724 && pf_rv(0x1262, PFT_U8) != 0 && pf_rv(0x1280, PFT_U8) == 0;
    /* batch 9: Dynamic Mill Cut Parameters 3205 - FUN_180168b70 (l.252576): 3438 Conventional feed rate enabled only when the
       cutting method combo is Zigzag (index 2 = record 0x1784 bit 1, FUN_180167720); FUN_180168c30: 3356 for styles 0 / 2 */
    if (d->dlg == 3205) {
        if (d->id == 3438) return ((int)pf_rv(0x1784, PFT_S32) & 2) != 0;
        if (d->id == 3356) return pf_style() == 0 || pf_style() == 2;
        return 0;
    }
    /* batch 9: Entry Motion 3219 - FUN_180169600 (mcmill_all.c l.252859) on the entry method m (combo = record 0x16bb): 1004 Helix radius for
       m < 3, 3226 Center helix on point for m = 0, 3210 for m = 2, 3211 for m = 4 / 5; 3216 Plunge angle when the radio is 0
       (record 0x16bd = 0), 3218 Entry pitch when it is 1 */
    /* batch 9: Contour Wall 3299 - HSTCtourWallPage MapToDlg / MapToOp (decomp/mcmill_all.c l.244447 / l.244493) enable nothing: always
       enabled (InitPage only clamps: >= 0, stock thickness >= CDynPockParm::GetMinMatThick) */
    if (d->dlg == 3299) return 1;
    if (d->dlg == 3219) {
        int m = (int)pf_rv(0x16bb, PFT_U8), r = (int)pf_rv(0x16bd, PFT_U8);
        switch (d->id) {
        case 1004: return m < 3;
        case 3210: return m == 2;                 /* medax_de: Trochoidal loop radius 0x16c8 (P+0x98, mcce_medent.c 0x400) */
        case 3211: return m == 4 || m == 5;       /* medax_de: Additional slot width 0x1690 (P+0xa0, mcce_medent.c 0x1000) */
        case 3215: return 1;
        case 3218: return r == 1;
        }
        return 0;
    }
    if (d->dlg != 2942) return 0;
    int st = (int)pf_rv(0x1228, PFT_S16), dyn = st == 5, one = st == 3, zz = st == 0, mv2 = (int)pf_rv(0x122a, PFT_S16) == 2;
    switch (d->id) {
    case 1748: case 1752: case 1704: case 1708: return !dyn;
    case 3356: return dyn;
    case 2027: return !dyn;
    case 1758: return !dyn && pf_rv(0x1288, PFT_U8) == 0;
    case 1722: case 1730: case 25113: case 1135: return !one;
    case 1737: case 1743: case 2028: return !one && !dyn;
    case 1857: return zz;
    /* Feed rate between cuts: Zigzag and not Rapid (FUN_18016caf0 cVar10); its F word is posted only with MCAM_FACE_LINK_FEED=1
       (ours, cam2d_face.c), so live only with the switch */
    case 1846: return zz && !mv2 && cam_face_link_feed_on();
    case 1710: return zz && !mv2 && cam_face_link_feed_on() && pf_rv(0x1298, PFT_U8) != 0;
    }
    return 1;
}
static void pf_emit(PfCtx *c, int dlg, int ox, int oy) {
    for (int i = 0; i < (int)(sizeof PF_DEF / sizeof PF_DEF[0]); i++) {
        const PfDef *d = &PF_DEF[i];
        if (d->dlg != dlg || pf_hidden(d)) continue;
        char file[64] = "";
        if (d->kind == PFK_PIC) { if (!pf_pic(d, ox, oy, file, sizeof file)) continue; }   /* a picture this program has none for: not drawn */
        PfShown *s = pf_add(c, d->kind, d->id, d->x + ox, d->y + oy, d->w, d->h, d->text);
        if (!s) return;
        if (dlg == 3001 && d->id == 2101) s->id = 3101;   /* ours: control 2101 of child panel 3001 shares its id with the host page's Pocket type combo (pf_def_shown maps it back) */
        snprintf(s->pic, sizeof s->pic, "%s", file);
        /* captions set by code */
        if (dlg == 3205 && d->id == 3327) snprintf(s->text, sizeof s->text, "%s", pf_style() == 2 ? "Compensation direction" : "Cutting method");   /* mcmillres 363 / 362 (FUN_180167720) */
        if (dlg == 2942 && d->id == 2028) snprintf(s->text, sizeof s->text, "%s", pending.p.face_style == 0 ? "Even number of passes" : "Reverse direction of last pass");   /* 2710 / 2711 */
        if (dlg == 25595 && pending.p.type == CAM_DRILL) {   /* batch 8: DrillCyclePageBase::EnableControls (l.444402) labels from the post text */
            int k = d->id == 1136 ? 3 : d->id == 1107 ? 7 : d->id == 1110 ? 8 : d->id == 1112 ? 9 : d->id == 1133 ? 10 : d->id == 1138 ? 11 : 0;
            if (k) snprintf(s->text, sizeof s->text, "%s", nc_drill_text(pending.p.drill_cycle, k, NULL));
        }
        if (dlg == 2824 && d->id == 1412) snprintf(s->text, sizeof s->text, "%s", pending.p.thread_internal ? "Major thread diameter" : "Minor thread diameter");   /* 2643 / 2644 (FUN_180192db0) */
        if (dlg == 6555 && misc_desc_idx(d->id)) {   /* batch 11: SetDialogText - the post text's description (read only) */
            int ix = misc_desc_idx(d->id); const char *t = pv_misc_text(ix, NULL);
            snprintf(s->text, sizeof s->text, "%s", t ? t : ""); s->live = 0; s->nd = t ? 0 : 1;
            if (t) snprintf(s->tip, sizeof s->tip, "post text page [misc integers] entry %d (ours: the [CTRL_MILL|DEFAULT] text of Generic Fanuc 3X Mill.pst / "
                            "MPFAN.pst stands in for the part's own control text, which is compressed - NOT DECODED)", ix);
            else snprintf(s->tip, sizeof s->tip, "the post text has no entry %d: its default description (CPostText::GetTextStructure's resource id) "
                          "is NOT DECODED", ix);
            continue;
        }
        if (d->kind == PFK_STATIC || d->kind == PFK_GROUP || d->kind == PFK_PIC) continue;
        if (d->kind == PFK_PUSH && dlg == 2834 && d->id == 1098) {   /* Position... (FUN_180190d80: Manual, square point off) */
            s->live = pending.p.tabs > 0 && !pf_rv(0x1381, PFT_U8) && !pf_rv(0x13ba, PFT_U8); continue; }
        if (d->kind == PFK_PUSH && dlg == 3013 && (d->id == 1391 || d->id == 1392)) {   /* the adjust copy buttons (adj_copy): leads on */
            s->live = pending.p.type == CAM_CONTOUR && pending.p.lead_on; continue; }
        if (d->kind == PFK_PUSH && dlg == 3013 && (d->id == 25502 || d->id == 25504)) {   /* the lead copy buttons (lead_copy): the source group on */
            s->live = pending.p.type == CAM_CONTOUR && pending.p.lead_on && (d->id == 25502 ? pending.p.lead_in_on : pending.p.lead_out_on); continue; }
        if (d->kind == PFK_PUSH || d->kind == PFK_TRACK) { s->live = 0; continue; }   /* buttons / sliders: handlers not ported */
        double v = 0; int live = 0, bound = pf_bget(d, &v, &live), cust = 0, off = -1;
        if (!bound) cust = pf_custom(d, &v, &live);
        if (!bound && !cust) {
            off = pf_off(d);
            if (off >= 0 && d->val != PFV_NONE) v = pf_rv(off, d->type);
            live = off >= 0 && d->st <= PFS_PART && d->val != PFV_NONE && d->val != PFV_CUSTOM && pf_honoured(off)
                && (d->en == 0 || (d->en > 0 && pf_group_on(dlg, d->en)) || (d->en < 0 && pf_en_rule(d)));
        }
        if (live && !pf_pock_rule(d)) live = 0;   /* batch 6: the Pocket Roughing page's decoded per-method enables */
        if (dlg == 6555 && misc_val_idx(d->id) && pf_rv(PV_MISC_SET, PFT_U8) == 0) v = pv_misc_value(misc_val_idx(d->id));   /* batch 11: InitPage ->
                                                    HideDisableButtons -> OnPostValsPb: with 2252 on the page shows the post text's values */
        if (dlg == 10318 && ((d->id == 10368 && !pf_group_on(10318, 10385)) || (d->id == 10382 && !pf_group_on(10318, 10386)))) v = 0;   /* batch 10:
                                                    InitPage 0x1809a8810 shows a tolerance only while its group is on */
        s->live = live; s->nd = !live && d->nd;
        if (dlg == 25595 && d->id == 1089 && bound) s->nd = 0;   /* batch 8: the dwell's unit is the post's (P word, seconds in LinuxCNC) */
        { const char *why = pf_engine_nd(d);   /* batch 9: a leading "N:" = decoded, greyed for the posted-NC rule (no NOT DECODED marker) */
          if (why) { int nn = !strncmp(why, "N:", 2); s->live = 0; s->nd = !nn; snprintf(s->tip, sizeof s->tip, "%s", why + (nn ? 2 : 0)); } }
        if (dlg == 25595 && pending.p.type == CAM_DRILL && !s->live) {   /* batch 8: why a drill field is greyed */
            int k = d->id == 1089 ? 3 : d->id == 1072 ? 7 : d->id == 1077 ? 8 : d->id == 1082 ? 9 : d->id == 1087 ? 10 : d->id == 1131 ? 11 : 0;
            if (k && !pf_drill_pt(k)) snprintf(s->tip, sizeof s->tip, "disabled by the post text for this cycle (Generic Fanuc 3X Mill.pst page "
                                               "[CTRL_MILL|GENERIC FANUC 3X MILL], entry %d; DrillCyclePageBase::EnableControls)", k);
            else if (k == 3) snprintf(s->tip, sizeof s->tip, "the dwell (G82 / G89 P) is posted only with MCAM_DRILL_CYCLES=1 (formats/post.md)");
            else if (k == 11) snprintf(s->tip, sizeof s->tip, "Fine Bore (shift): the post's G76 is LinuxCNC's threading cycle - not posted");
            else if (d->id == 1951 || (d->id >= 1696 && d->id <= 1734)) snprintf(s->tip, sizeof s->tip, "custom drill cycles (8-19) are not posted; "
                                               "their parameters are the post's (text page 42)");
        }
        if (dlg == 10318 && s->live && ((d->id == 10401 && pf_rv(0xca9, PFT_U8) == 0) || ((d->id == 10398 || d->id == 10399) && pf_rv(0xca9, PFT_U8) != 0)))
            s->live = 0;   /* ArcFilterRefinePage::ValidateRftpDialogControls 0x1809aaf70 / OnBnClickedCheckLimitMaxSegmentLength 0x1809a9990: the
                              segment length with Use Fixed Segment Length, Shift points randomly / Minimize number of points without it */
        if (dlg == 10318 && d->id == 10391 && s->live && pf_rv(0x33f, PFT_U8) == 0) s->live = 0;   /* batch 10: the tightening value needs a
                                                    Tighten radio (UpdateEnableStareForTighteningGroup 0x1809aa990) */
        if (dlg == 10318 && d->id == 10366 && pending.p.type == CAM_POCKET) { s->live = 0; s->nd = 1; }   /* the tsv: pocket only when
                                                    !ctour_has_curve - that test is NOT DECODED */
        if (!bound && !cust && (off < 0 || d->val == PFV_NONE)) {   /* no field for this opcode / not a field */
            s->live = 0; if (d->kind == PFK_EDIT || d->kind == PFK_COMBO) s->text[0] = 0;
            if (s->nd == 1) { size_t L = strlen(s->text); snprintf(s->text + L, sizeof s->text - L, "%sNOT DECODED", L ? " - " : ""); }
            continue;
        }
        switch (d->kind) {
        case PFK_EDIT: pf_num(s->text, sizeof s->text, pf_rad(d) ? v * 180 / M_PI : v, bound ? PFT_F64 : d->type);
            if (bound && (d->type == PFT_S16 || d->type == PFT_S32)) snprintf(s->text, sizeof s->text, "%d", (int)v);
            break;
        case PFK_CHECK: case PFK_CGROUP: case PFK_RADIO:
            s->on = d->val == PFV_INV ? v == 0 : d->val == PFV_EQ ? v == d->k : d->val == PFV_CUSTOM && !bound ? v != 0 : v != 0;
            break;
        case PFK_COMBO: case PFK_LIST: {
            int code[PF_MAXITEMS], n = bound ? pf_bitems(d, s->items, code) : 0;
            if (!n && cust) n = pf_custom_items(d, s->items, code);
            if (!n) {   /* the decoded items "code=Text|..." (pointers into the table's string) */
                static char buf[64][PF_MAXITEMS][120]; static int bi; char (*b)[120] = buf[bi++ & 63];   /* an item whole (48 cut "... exceeding a distance": textfit 2026-11-17) */
                const char *q = d->items;
                while (q && *q && n < PF_MAXITEMS) { int cd = atoi(q); const char *eq = strchr(q, '='), *bar = strchr(q, '|'); if (!eq) break;
                    int L = bar ? (int)(bar - eq - 1) : (int)strlen(eq + 1); if (L > 119) L = 119;
                    memcpy(b[n], eq + 1, L); b[n][L] = 0; s->items[n] = b[n]; code[n] = cd; n++; q = bar ? bar + 1 : NULL; }
            }
            s->nitems = n; s->cur = -1;
            for (int k = 0; k < n; k++) if (code[k] == (int)v) s->cur = k;
            if (s->cur >= 0) snprintf(s->text, sizeof s->text, "%s", s->items[s->cur]); else snprintf(s->text, sizeof s->text, "%d", (int)v);
            if (s->cur < 0 && !(dlg == 2942 && d->id == 1857)) s->live = 0;   /* 1857: the defaults hold -1, Mastercam's combo then shows no
                                                    selection (CB_SETCURSEL -1, FUN_18016ba50) and stays usable */
            if (s->cur < 0 && dlg == 2942 && d->id == 1857) s->text[0] = 0;
            if (s->cur < 0 && (dlg == 6546 || dlg == 6548)) s->text[0] = 0;   /* batch 11: an empty combo (V9 style position) / a list */
            if (d->st == PFS_PART && !bound && !s->live) s->nd = 1;   /* item strings NOT DECODED */
            break; }
        }
        if (s->nd == 1) { size_t L = strlen(s->text); snprintf(s->text + L, sizeof s->text - L, "%sNOT DECODED", L ? " - " : ""); }   /* the marker */
    }
}
/* Pocket type = Island facing (record 0x13fa = 2): the page 2940 shows HSTPocketFacePage 3002 */
static int pf_pocket_facepanel(void) { if (pending.p.type != CAM_POCKET || !pending.p.pk_ok) return 0; int t = (int)pf_rv(0x13fa, PFT_S16); return t == 1 || t == 2; }
/* the child panel 3005 shows (HSTPocketEntryPage: 3010 helix 0xbc2 / 3011 ramp 0xbc3 in static 2990), 0 = none */
static int pf_entry_child(void) { int on = (int)pf_rv(0x12a0, PFT_U8), ty = (int)pf_rv(0x12a2, PFT_S16); return !on ? 0 : ty == 1 ? 3011 : ty == 0 ? 3010 : 0; }
int pf_ctls(PfShown *out, int cap) {
    int pg = pp_page_shown(), dlg = pf_page_dialog(pg);
    if (!dlg) return 0;
    PfCtx c = { out, cap, 0 };
    pf_emit(&c, dlg, 0, 0);
    /* the 2D chamfer child page 2996 (ToolpathPageBase ctor 0xbb4, mcmill_all.c l.240011; the contour page sets it at
       l.240319) in the contour page's frame 2990 (ours: the frame it is placed in) */
    if (dlg == 2938 && pending.p.contour_type == 1) { pf_emit(&c, 2996, 235, 50); pf_ours(&c, 2996, 235, 50); }
    if (dlg == 2940 && pending.p.pk_ok && (int)pf_rv(0x13fa, PFT_S16) == 3) pf_emit(&c, 3146, 228, 52);   /* 2940's frame 2990 at (228, 52); child 0xc4a: FUN_17b220 / FUN_17b8b0 (l.264027 / 264316) */
    if (dlg == 2938 && pending.p.contour_type == 0 && pending.p.ct_rtype == 5) pf_emit(&c, 2999, 235, 50);   /* HSTContourRemachPage (ToolpathPageBase ctor 2999, mcmill_all.c l.243163; the page's subdialog l.240679), same frame */
    if (dlg == 2938 && pending.p.contour_type == 0 && pending.p.ct_rtype == 4) pf_emit(&c, 2997, 235, 50);   /* the Ramp child (0xbb5), same frame */
    if (dlg == 2938 && pending.p.contour_type == 0 && pending.p.ct_rtype == 6) pf_emit(&c, 2998, 235, 50);   /* the Oscillate child (0xbb6) */
    if (dlg == 2940 && (int)pf_rv(0x13fa, PFT_S16) == 4) pf_emit(&c, 3001, 228, 52);   /* HSTPocketOpenPage 0xbb9 (FUN_18017b220 l.264150, by pocket type) in the page's frame, static 2990 (228, 52) */
    if (dlg == 3005) { int ch = pf_entry_child(); if (ch) pf_emit(&c, ch, 13, 24); }   /* static 2990 (13, 24) */
    /* HSTPocketCutPage's combo 2101 items 1 Facing / 2 Island facing (FUN_18017b220 l.264027: the 2nd / 3rd child, FUN_18017d530 = HSTPocketFacePage, dialog 0xbba = 3002, flag +0x5da0 = 0 / 1: control 0x504 "Stock above islands" EnableWindow, FUN_18017e260 l.265869; FUN_18017b8b0 l.264316 shows it in static 2990 (228, 52)).
       FUN_18017da90 l.265876-265881 (page init) and l.266032-266036 (page activate, record not edited): the overlap distance recomputed as % x D / 100 when both are non-zero; FUN_18017db70 stores it */
    if (dlg == 2940 && pf_pocket_facepanel()) {
        double pct = pf_rv(0x1380, PFT_F64), D = pending.p.tool_dia;
        if (D != 0.0 && pct != 0.0 && fabs(pf_rv(0x1388, PFT_F64) - pct * D / 100.0) > 1e-12) pf_put(0x1388, PFT_F64, pct * D / 100.0);
        pf_emit(&c, 3002, 228, 52);
    }
    pf_ours(&c, dlg, 0, 0);
    pf_marks(&c, dlg);
    return c.n;
}
/* a control's change: id + value (edit: the number; check: 0 / 1; radio: ignored; combo / list: the item index) */
static const PfDef *pf_def_shown(int id) {
    int pg = pp_page_shown(), dlg = pf_page_dialog(pg);
    if (dlg == 2940 && pf_rv(0x13fa, PFT_S16) == 4) {   /* the Open pocket child panel 3001 (3101 = its control 2101, see pf_emit) */
        for (int i = 0; i < (int)(sizeof PF_DEF / sizeof PF_DEF[0]); i++) if (PF_DEF[i].dlg == 3001 && PF_DEF[i].id == (id == 3101 ? 2101 : id) && id != 2101) return &PF_DEF[i];
    }
    int ch = dlg == 2938 && pending.p.contour_type == 1 ? 2996 : dlg == 2938 && pending.p.contour_type == 0 && pending.p.ct_rtype == 4 ? 2997 : dlg == 2938 && pending.p.contour_type == 0 && pending.p.ct_rtype == 5 ? 2999 : dlg == 2938 && pending.p.contour_type == 0 && pending.p.ct_rtype == 6 ? 2998 : dlg == 2940 && pending.p.pk_ok && (int)pf_rv(0x13fa, PFT_S16) == 3 ? 3146 : dlg == 3005 ? pf_entry_child() : dlg == 2940 && pf_pocket_facepanel() ? 3002 : 0;
    for (int i = 0; i < (int)(sizeof PF_DEF / sizeof PF_DEF[0]); i++)
        if ((PF_DEF[i].dlg == dlg || (ch && PF_DEF[i].dlg == ch)) && PF_DEF[i].id == id && !pf_hidden(&PF_DEF[i])) return &PF_DEF[i];
    return NULL;
}
/* batch 10: the Line/Arc Filtering group toggled (ArcFilterRefinePage::OnBnClickedStaticLineArcFilter 0x1809a9b10 and the rescale of
   UpdateLocalOp 0x1809aaaf0; the page's total = cut + filter + smoothing (InitPage 0x1809a8810, the smoothing share only when its
   group is on, the filter share only when its group is on)). On: the filter tolerance 0x320 = 5 % of the total, the cut and
   smoothing tolerances scaled by 1 - filter / (cut + smoothing); off: the filter tolerance 0, the others scaled back to the total.
   Not ported (NOT DECODED here): KeepTolerancesConsistent 0x1809a8ef0 for an edit of one tolerance, the sliders / % edits */
static void pf_filter_group(int on) {
    const PfDef *cd = NULL;
    for (int i = 0; i < (int)(sizeof PF_DEF / sizeof PF_DEF[0]); i++) if (PF_DEF[i].dlg == 10318 && PF_DEF[i].id == 10366) cd = &PF_DEF[i];
    int coff = cd ? pf_off(cd) : -1;
    double cut = coff >= 0 ? pf_rv(coff, PFT_F64) : 0, sm = pf_rv(0xca8, PFT_U8) != 0 ? pf_rv(0xc98, PFT_F64) : 0;
    double fl = on ? 0 : pf_rv(0x320, PFT_F64);          /* InitPage: the filter share is 0 while its group is off */
    if (cut <= 1e-05) cut = 1e-05;
    double tot = cut + fl + sm, ncut = cut, nsm = sm, nfl;
    if (on) {
        nfl = tot * 0.05;
        if (1e-07 <= fabs(((tot - nfl) - cut) - sm)) { double k = 1.0 - nfl / (sm + cut); ncut = cut * k; nsm = sm * k; }
    } else {
        nfl = 0;
        if (1e-06 < fabs(tot - nfl - cut - sm) && cut + sm > 0) { double k = tot / (cut + sm); ncut = cut * k; nsm = sm * k; }
    }
    if (fabs(nfl - pf_rv(0x320, PFT_F64)) > 1e-15) pf_put(0x320, PFT_F64, nfl);
    if (coff >= 0 && fabs(ncut - pf_rv(coff, PFT_F64)) > 1e-15) pf_put(coff, PFT_F64, ncut);
    if (pf_rv(0xca8, PFT_U8) != 0 && fabs(nsm - pf_rv(0xc98, PFT_F64)) > 1e-15) pf_put(0xc98, PFT_F64, nsm);
}
/* the Smoothing Settings group check 10386 (record 0xca8; 2026-11-08): ArcFilterRefinePage::OnBnClickedStaticSmoothing 0x1809a9c40, then
   KeepTolerancesConsistent 0x1809a8ef0 (decomp/opdlg_mccore_arcfilter.c l.1495-1533, l.960-1300) with the page's shares (InitPage: the
   smoothing share only while its group is on, the filter share only while its group is on). On: the smoothing tolerance 0xc98 = 5 % of
   the total, the cut and filter tolerances scaled by 1 - smoothing / (cut + filter) (base 12 -> 9: unchanged after); off: 0xc98 = 0
   (UpdateLocalOp 0x1809aaaf0 writes the page's share), KeepTolerancesConsistent base 10: with the filter on the cut tolerance =
   total / (cut + filter) x cut and the filter the rest, without it the cut tolerance = the total */
static void pf_smooth_group(int on) {
    const PfDef *cd = NULL;
    for (int i = 0; i < (int)(sizeof PF_DEF / sizeof PF_DEF[0]); i++) if (PF_DEF[i].dlg == 10318 && PF_DEF[i].id == 10366) cd = &PF_DEF[i];
    int coff = cd ? pf_off(cd) : -1, fon = pf_rv(0x33a, PFT_U8) != 0;
    double cut = coff >= 0 ? pf_rv(coff, PFT_F64) : 0, fl = fon ? pf_rv(0x320, PFT_F64) : 0, sm = on ? 0 : pf_rv(0xc98, PFT_F64);
    if (cut <= 1e-05) cut = 1e-05;                        /* InitPage: 0x9218 below 1e-05 -> 1e-05 */
    double tot = cut + fl + sm, ncut = cut, nfl = fl, nsm;
    if (on) {
        nsm = tot * 0.05;
        if (1e-07 <= fabs(((tot - cut) - nsm) - fl)) { double k = 1.0 - nsm / (fl + cut); nfl = fl * k; ncut = cut * k; }
        if (!fon) { ncut = tot / (ncut + nsm) * ncut; nsm = tot - ncut; }
    } else {
        nsm = 0;
        if (fon) { ncut = tot / (cut + fl) * cut; nfl = tot - ncut; } else ncut = tot;
    }
    if (fon && fabs(nfl - pf_rv(0x320, PFT_F64)) > 1e-15) pf_put(0x320, PFT_F64, nfl);
    if (coff >= 0 && fabs(ncut - pf_rv(coff, PFT_F64)) > 1e-15) pf_put(coff, PFT_F64, ncut);
    if (fabs(nsm - pf_rv(0xc98, PFT_F64)) > 1e-15) pf_put(0xc98, PFT_F64, nsm);
}
/* a control's current value as the page shows it (pf_ctls' read: the bound value, else the custom one, else the record field) */
static double pf_value_of(const PfDef *d) {
    double v = 0; int live = 0;
    if (pf_bget(d, &v, &live) || pf_custom(d, &v, &live)) return pf_rad(d) ? v * 180 / M_PI : v;
    int off = pf_off(d); if (off >= 0 && d->val != PFV_NONE) v = pf_rv(off, d->type);
    return pf_rad(d) ? v * 180 / M_PI : v;
}
/* Contour Lead In/Out 3013 copy buttons: "->" 25502 FUN_180172f40 / "<-" 25504 FUN_180172d70 (decomp/mcmill_all.c; the page's message
   map is built at run time, l.256848 / l.256850): with the source side's group on, the other group is switched on and the side's
   values copied over - the Perpendicular / Tangent radio, line length % / absolute, ramp height, arc radius % / absolute, sweep, helix
   height, the override feed value, and the checks first / last depth only, use point, use point depth, plunge after first / retract
   before last move, override feed (the members mapped to controls by the page's DDX, FUN_180159.. l.256540-256613) */
void pf_set(int id, double v);
static void lead_copy(int to_exit) {
    static const int E[] = { 25166, 25165, 25369, 25172, 25171, 25174, 25219, 25163, 25159, 25169, 25170, 25333, 25162 };
    static const int X[] = { 25181, 25180, 25370, 25186, 25185, 25187, 25220, 25178, 25179, 25183, 25184, 25375, 25177 };
    PfShown sh[160]; int n = pf_ctls(sh, 160);
    int grp_src = to_exit ? 3391 : 3392, grp_dst = to_exit ? 3392 : 3391, src_on = 0, perp = 0;
    for (int i = 0; i < n; i++) { if (sh[i].id == grp_src && sh[i].kind == PFK_CGROUP) src_on = sh[i].on; if (sh[i].id == (to_exit ? 25167 : 25182) && sh[i].kind == PFK_RADIO) perp = sh[i].on; }
    if (!src_on) return;
    pf_set(grp_dst, 1);
    pf_set(to_exit ? (perp ? 25182 : 25188) : (perp ? 25167 : 1160), 1);
    for (int k = 0; k < (int)(sizeof E / sizeof E[0]); k++) {
        int si = to_exit ? E[k] : X[k], di = to_exit ? X[k] : E[k];
        const PfDef *ds = pf_def_shown(si); if (!ds) continue;
        double sv = 0; int kind = ds->kind;
        if (kind == PFK_CHECK) { for (int i = 0; i < n; i++) if (sh[i].id == si && sh[i].kind == PFK_CHECK) sv = sh[i].on; }
        else sv = pf_value_of(ds);
        pf_set(di, sv);
    }
}
/* Adjust start / end of contour copy buttons: "->" 1391 FUN_18015a580 / "<-" 1392 FUN_18015a4e0 (the page's run-time message map,
   l.241761-241771): with the source side's check (1225 start / 1226 end) on, the other side's check is set and Extend / Shorten,
   the length % and the absolute length copied (members via the DDX FUN_180159160 l.241605) */
static void adj_copy(int to_end) {
    PfShown sh[160]; int n = pf_ctls(sh, 160), src = to_end ? 1225 : 1226, on = 0, ext = 0;
    for (int i = 0; i < n; i++) { if (sh[i].id == src && sh[i].kind == PFK_CHECK) on = sh[i].on; if (sh[i].id == (to_end ? 1300 : 1304) && sh[i].kind == PFK_RADIO) ext = sh[i].on; }
    if (!on) return;
    pf_set(to_end ? 1226 : 1225, 1);
    pf_set(to_end ? (ext ? 1304 : 1167) : (ext ? 1300 : 1164), 1);
    const PfDef *a = pf_def_shown(to_end ? 1990 : 1992), *b = pf_def_shown(to_end ? 1302 : 1306);
    if (a) pf_set(to_end ? 1992 : 1990, pf_value_of(a));
    if (b) pf_set(to_end ? 1306 : 1302, pf_value_of(b));
}
void pf_set(int id, double v) {
    PfShown sh[160]; int n = pf_ctls(sh, 160), k = -1;
    for (int i = 0; i < n; i++) if (sh[i].id == id && sh[i].kind != PFK_STATIC && sh[i].kind != PFK_PIC && sh[i].kind != PFK_GROUP) k = i;
    if (k < 0 || !sh[k].live) return;                         /* greyed / not on the page */
    if (sh[k].kind == PFK_PUSH) { if (id == 1098 && pf_page_dialog(pp_page) == 2834) tb_start();
        if ((id == 25502 || id == 25504) && pf_page_dialog(pp_page) == 3013) lead_copy(id == 25502);
        if ((id == 1391 || id == 1392) && pf_page_dialog(pp_page) == 3013) adj_copy(id == 1391);
        return; }   /* the live buttons' handlers */
    CamParams before = pending.p; OpHFlags hb = pp_hf; int ned = pf_ned; uint8_t edb[sizeof pf_ed]; memcpy(edb, pf_ed, sizeof pf_ed);
    if (sh[k].ours) {
        if (sh[k].kind == PFK_COMBO) { v = v >= 0 && v < sh[k].nitems ? v : sh[k].cur; if ((int)v == sh[k].cur) return; }
        if (sh[k].kind == PFK_CHECK && (v != 0) == (sh[k].on != 0)) return;
        pf_ours_set(id, v);
    } else {
        const PfDef *d = pf_def_shown(id); if (!d) return;
        double rv = v; int code[PF_MAXITEMS]; const char *it[PF_MAXITEMS];
        switch (sh[k].kind) {
        case PFK_EDIT: rv = pf_rad(d) ? v * M_PI / 180 : v; break;
        case PFK_CHECK: case PFK_CGROUP: rv = d->val == PFV_INV ? v == 0 : v != 0; break;
        case PFK_RADIO: if (sh[k].on) return; rv = d->val == PFV_EQ ? d->k : d->val == PFV_INV ? 0 : 1; break;   /* a click on the selected one changes nothing */
        case PFK_COMBO: case PFK_LIST: {
            double cv; int lv, nn = pf_bget(d, &cv, &lv) ? pf_bitems(d, it, code) : 0;
            if (!nn) nn = pf_custom_items(d, it, code);
            if (!nn) { const char *q = d->items; while (q && *q && nn < PF_MAXITEMS) { code[nn++] = atoi(q); q = strchr(q, '|'); if (q) q++; } }
            int i = (int)v; if (i < 0 || i >= nn) return; rv = code[i]; } break;
        }
        double bv; int bl;
        {   /* the value it holds now: no edit when unchanged (an untouched field is never patched; nuklear commits an edit box
               on leaving it, a Qt check box set to its state sends nothing) */
            double cv = 0; int cl, have = pf_bget(d, &cv, &cl) || pf_custom(d, &cv, &cl);
            if (!have) { int off = pf_off(d); have = off >= 0; if (have) cv = pf_rv(off, d->type); }
            if (sh[k].kind == PFK_RADIO && d->val == PFV_CUSTOM) have = 0;   /* 3005: the radio's own state was tested above */
            if (have && fabs(rv - cv) <= 1e-12 * (1 + fabs(cv))) return;
        }
        if (d->val == PFV_CUSTOM && !pf_bget(d, &bv, &bl)) pf_custom_set(d, rv);
        else if (!pf_bset(d, rv)) {
            int off = pf_off(d); if (off < 0) return;
            pf_put(off, d->type, rv);
            /* "[% of D] % [distance]" pairs (formats/opdlg_fields_notes.md F2 / F3: B = A x D / 100): the engine reads the % side,
               the distance written with it as Mastercam's handlers do */
            static const struct { int dlg, id; uint16_t aoff; } PAIR[] = { { 25597, 2078, 0x1270 }, { 25597, 2022, 0x1280 }, { 25597, 1719, 0x1288 },
                { 3002, 25324, 0x1388 }, { 3010, 2022, 0x12a8 }, { 3010, 1719, 0x12b0 }, { 3011, 2024, 0x1310 }, { 3011, 1720, 0x1318 },
                { 3001, 1975, 0x13e8 },   /* Open pocket overlap: FUN_180180fc0 l.267898 distance = D x % x 0.01 */
                /* batch 7: Face 2942 (FUN_18016cea0: distance = % x D x 0.01) */
                { 2942, 1722, 0x1238 }, { 2942, 1737, 0x1258 }, { 2942, 1748, 0x1248 }, { 2942, 1755, 0x1268 }, { 2942, 1704, 0x1278 } };
            for (int q = 0; q < (int)(sizeof PAIR / sizeof PAIR[0]); q++)
                if (PAIR[q].dlg == d->dlg && PAIR[q].id == d->id) pf_put(PAIR[q].aoff, PFT_F64, rv * pending.p.tool_dia / 100);
            /* batch 6: the distance side of a pocket pair edited: its % (A = B x 100 / D, as the lead pairs' distance edits,
               K(3013, 25165)) - the engine reads the % side */
            static const struct { int dlg, id; uint16_t poff; } RPAIR[] = { { 3002, 1372, 0x1380 }, { 3001, 1718, 0x13e0 } /* Open pocket: FUN_180180ee0 l.267864 % = distance x 100 / D */, { 3010, 1478, 0x12f8 }, { 3010, 1480, 0x1300 }, { 3011, 1961, 0x1360 },
                { 3011, 1965, 0x1368 },
                /* batch 7: Face 2942 distance edits -> % (bnci_face recomputes the distance from the %) */
                { 2942, 1730, 0x1240 }, { 2942, 1743, 0x1260 }, { 2942, 1752, 0x1250 }, { 2942, 1756, 0x1270 }, { 2942, 1708, 0x1280 } };
            for (int q = 0; q < (int)(sizeof RPAIR / sizeof RPAIR[0]); q++)
                if (RPAIR[q].dlg == d->dlg && RPAIR[q].id == d->id && pending.p.tool_dia > 0) pf_put(RPAIR[q].poff, PFT_F64, rv * 100 / pending.p.tool_dia);
            if (pending.p.type == CAM_THREAD) pf_thread_sync(&pending.p);   /* spring passes 0x12b0 */
            if (d->dlg == 1326 && pending.p.type == CAM_AREAROUGH && (d->id == 1263 || d->id == 1245 || d->id == 1246)) {
                /* the XY stepover group: leaving 1263 -> Maximum = % / 100 x D, Minimum = Maximum x 0.55 (FUN_18009d670 l.102806-102829);
                   leaving 1246 -> % = Maximum / D x 100 (FUN_18009d480 l.102724-102751); saving writes 0x13e0 = Minimum / Maximum x 100
                   (FUN_18009dc20 l.103108-103114). Not ported: the D <= tol fallback (defaults key 3 / 45), the page-leave check 7540 / 7541 */
                double D = pending.p.tool_dia;
                if (d->id == 1263 && D > 0) { pf_put(0x12c0, PFT_F64, rv / 100 * D); pf_put(0x12b8, PFT_F64, rv / 100 * D * 0.55); }
                if (d->id == 1246 && D > 0) pf_put(0x1360, PFT_F64, rv / D * 100);
                double mx = pf_rv(0x12c0, PFT_F64); if (mx > 0) pf_put(0x13e0, PFT_F64, pf_rv(0x12b8, PFT_F64) / mx * 100);
            }
            if (d->dlg == 10318 && d->id == 10385) pf_filter_group(rv != 0);   /* batch 10 */
            if (d->dlg == 10318 && d->id == 10386) pf_smooth_group(rv != 0);   /* 2026-11-08 */
            if (d->dlg == 6555 && d->id == 2252 && rv == 0) misc_post_values();   /* batch 11: OnAutoMiscCb -> HideDisableButtons -> OnPostValsPb */
        }
    }
    if (memcmp(&before, &pending.p, sizeof before) || memcmp(&hb, &pp_hf, sizeof hb) || ned != pf_ned || memcmp(edb, pf_ed, sizeof pf_ed)) pp_tstate[pp_page] = 1;   /* SetPageDataChanged */
}
#undef K
