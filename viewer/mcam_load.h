/* mcam_load - read geometry, tools, operations and stored toolpaths from Mastercam 2017 / X9 part files.
 * Formats: ../formats/mcam.md (all decoded from the Mastercam DLLs; no Mastercam code is used). */
#ifndef MCAM_LOAD_H
#define MCAM_LOAD_H
#include <stddef.h>
#include <stdint.h>

typedef struct { double x, y, z; } V3;

enum { K_GEOM, K_CUT, K_RAPID };

typedef struct {            /* one drawable polyline */
    int kind;               /* K_GEOM / K_CUT / K_RAPID */
    int op;                 /* operation id (toolpaths), 0 for geometry */
    int tool;               /* tool number (toolpaths) */
    uint8_t color;          /* Mastercam colour index (geometry) */
    uint8_t ln_style;       /* geometry: the entity's line style (ent +0x18, set_gl_style; 1 = solid), 1 for toolpaths */
    uint8_t width;          /* geometry: the entity's line width (ent +0x10, set_gl_width), 1 for toolpaths */
    int level;              /* Mastercam level (geometry) */
    int first, count;       /* range in the point array */
    uint8_t blanked;        /* geometry: its entity's sel bit 0x100 (Ent.blanked): not drawn, not picked (draw_ent, mcam_ent_blank) */
} Poly;

typedef struct {
    int id;                 /* op id (NCI section id) */
    int seq;                /* Operations Manager order (op entity +0xe6) */
    int opcode;             /* op entity +0x6c */
    int tool;               /* tool number used (from NCI) */
    char comment[64];
    char type[48];          /* from opcode */
    int first_poly, npolys; /* polylines of this op (toolpaths are grouped per op after loading) */
    long npts;              /* toolpath points of this op */
    uint8_t *raw; uint32_t raw_n;   /* the OP_ID record as read (parameters: ../formats/mcam.md, cam_fileop.c); NULL for new ops */
    int stale;              /* its tool was changed here after Mastercam made the stored toolpath: not posted */
    int regen;              /* dirty (op +0x94): record +0xfc as read, or marked here (Mark2dHSTOpsRefMachStockDef4Regen / a changed op, cui_dirty.c): the save writes
                               record +0xfc = 1 (dirty) and +0xff = 0 */
    int slot;               /* TlMgr slot of its tool: OP_ID +0x5ac (formats/part_tools.md 1.1); -1 = not read */
    uint8_t *nci; size_t nci_n;   /* its NCI section (-2 .. -3 records, file format) rewritten here (Reverse toolpath); NULL = as stored */
    int nrev;               /* times Reverse toolpath ran on it here (the project file replays them) */
    int postv, postv_groups; /* batch 11: its post values (coolant / canned text / misc values, cam_postvals.c) were edited here: raw holds
                               them and nci their NCI records (pv_nci_rewrite); groups = PVG_* (the project file replays them) */
} Op;

typedef struct {            /* a TP_ID record: an operation's reference to geometry (kinds: 4 surface / solid / mesh / CAD file, 6 chain
                               header, 7 chained entity, 1 / 27 points, 30 tab). Layout: formats/surface_selection.md 2.1. The offsets
                               below are a 0x388-byte record's (X9 / 2017); in a 0x398-byte one (Mastercam 2018) every field from +0x98
                               on is 0x10 later (mcam_load.c, tp_sh) */
    int op_id, kind;
    int32_t ent;            /* referenced ent_idn (+0x6c) */
    uint32_t cls;           /* +0x74 the selection class (0x100 for every kind 4 record, solids too; 1 for wireframe picks) */
    uint32_t ecls;          /* +0x78 the referenced entity's class: 0x100 surface, 0x10000 solid, 0x100000 mesh, 0x40000 CAD file */
    int64_t eptr;           /* +0x80 the referenced entity's eptr (kind 6: its first member record's) */
    unsigned flags;         /* kind 4: +0x98 u16 - 0x4 check, 0x20 gouge, 0x1 / 0x2 (msurf_ent_count; their meaning NOT DECODED) */
    int link;               /* kind 4 solid: +0x9a i16 the SldToolPOp link id in the solid's history (surface_selection.md 2.3) */
    int chain, index;       /* +0x98 chain number, +0x9c index */
    int rev;                /* kind 7: +0xa8 = 1 entity traversed against its direction */
    int role;               /* kind 6: +0xe0 chain role (2D: 0 machining region, 4 avoidance ...; 3D: 2 containment, 3 curves / blend chains) */
    double xyz[3];          /* kinds 1 / 27: point (+0x98) */
    double t;               /* kind 30: parameter on the entity (+0xc8) */
    double tab_w, tab_h, tab_ang;   /* kind 30: the tab's own tab_list_rec values (formats/contour_100_plan.md 2): +0xd0 width, +0xd8
                               thickness, +0xf8 ramp angle, +0xe0 Full (byte), +0xe4 Ramp (short), +0x110 edited (byte; tab_list_rec
                               +0x78, read by mccore edit_operation l.3022-3046) */
    int tab_full, tab_ramp, tab_ed;
    int subst1;             /* ours (session): tp_substitute_ent re-pointed this reference to F.surfs[subst1 - 1] (Untrim / Remove Boundary,
                               core_ui cui_srfedit.c); in force while the surface it names (ent) is deleted and that one lives; 0 = none */
    int pmode;              /* kinds 1 / 27: point mode byte +0xb0 (ent_tp +0xb0: 0x43 'C' = the arc's centre - bnci_threadmill
                               0x180234c80 / FUN_180192050 take the arc's diameter only then; 0x50 'P' a point) */
} TpRef;
/* a kind 4 reference's role by its flags, as msurf_ent_count counts them (decomp/mcmsurf_all.c l.16410-16418, formats/surface_selection.md
   2.1 / 2.6): 0x20 gouge, else 0x4 check, else bit 0x1 without 0x2 is counted apart (TpEntTypeStats [2] / [6] ...; its meaning NOT DECODED),
   else drive. RemoveSelectedDriveCheck (l.6449) takes "no 0x4 / 0x20" as the drive list. */
enum { TP_REF_DRIVE = 0, TP_REF_CHECK = 1, TP_REF_GOUGE = 2, TP_REF_FLAG1 = 3 };
static inline int tp_ref_role(unsigned flags) { return flags & 0x20 ? TP_REF_GOUGE : flags & 0x4 ? TP_REF_CHECK : (flags & 1) && !(flags & 2) ? TP_REF_FLAG1 : TP_REF_DRIVE; }
enum { TP_ECLS_SURF = 0x100, TP_ECLS_SOLID = 0x10000, TP_ECLS_MESH = 0x100000, TP_ECLS_FILE = 0x40000 };   /* TpRef.ecls (+0x78) */

typedef struct {            /* a cutting tool: from the part (TL_ID record) or a tool library (see toollib.h) */
    int num; double dia; char name[64];
    int type;               /* Mastercam mill tool type (../formats/tooltypes_mill.tsv): 3 drill, 10 flat, 11 ball, 19 bull ... */
    double corner_r;        /* corner radius (ball = dia/2) */
    int rad_type;           /* record +6 (Tool Manager «Rad. Type», toolmanager_ui.md 1.5.3): 0 None, 1 Corner, 2 Full */
    double tip_angle;       /* drills / spot / countersink: included angle; chamfer mill: angle per side (degrees) */
    double tip_dia;         /* chamfer mill / thread mill flat tip diameter (libraries only) */
    double tl_tip_dia;      /* a part tool's TL_ID tool +0x3f8: the tip diameter engrave.dll reads (formats/engrave.md 3.1) */
    double pitch;           /* taps / thread mills */
    int dia_off, len_off;   /* D / H offset numbers */
    int rpm; double feed, plunge, retract;
    int flutes; double flute_len, shoulder_len, oal, shoulder_dia, arbor_dia;   /* geometry (libraries only) */
    int coolant;            /* 0 off, 1 flood */
    int coolant_style;      /* TlCoolant.OldStyle as read (8 off, 16 flood, 32 mist, 64 thru; other values NOT DECODED), 0 = none read */
    int is_metric;          /* TlAssemblyItem.IsMetric / op_tool_info +0x258 */
    int tool_mat;           /* cutter material 1..6 (op_tool_info +0x54; feeds_speeds.md), 0 = not set */
    unsigned char guid[16]; int has_guid;   /* .tooldb TlAssemblyItem.ID (Guid.ToByteArray order) of a library tool */
    int cycle; double peck1, peck2, chipbreak, dwell;                            /* hole making defaults */
    /* .tooldb geometry / properties edited by the Define Tool wizard (formats/toolmanager_ui.md 2.6 / 2.9; column per field) */
    int radius_type;        /* TlRadiusType = CornerTypeList: 0 none, 1 chamfer, 2 corner radius, 3 full radius, 4 roundover */
    double taper_angle;     /* TlToolEndmill / TlToolBoring.TaperAngle */
    double chamfer_len;     /* TlToolEndmill / TlToolReamer.ChamferLength */
    int shank_type;         /* TlToolMill.TlShankType: 0 straight, 1 tapered, 2 reduced neck */
    double taper_len, neck_dia;                 /* TlToolMill.TaperLength / NeckDiameter */
    int sec_radius_type; double sec_corner_r, sec_chamfer, sec_chamfer_angle;   /* TlToolSlotMill (upper corner) */
    double shoulder_angle, drill_dia, drill_len;  /* TlToolDrill.ShoulderAngle, TlToolCenterDrill.DrillDiameter / DrillLength */
    double pilot_len, pilot_dia;                /* TlToolCounterBore */
    double shift;           /* TlToolBoring.Shift */
    int thread_shape, nstarts; double root_dia, thread_angle;   /* TlToolThreading.TlThreadShape / NStarts / RootDiameter / ThreadAngle */
    double barrel_len, profile_r;               /* TlToolBarrelMill */
    double lower_r, upper_r, lower_taper, upper_taper, relief_taper;   /* TlToolFeedMill */
    double req_pilot_dia, helix_angle; int helix_type, spindle_dir;   /* TlToolMill.RequiredPilotDiameter / HelixAngle / TlHelixType / TlSpindleDir */
    int head_num;           /* TlTool.ToolStation (HeadNumber, -1 = none) */
    double sfm_adj, fpt_adj;                    /* TlOpParams.MaterialSFMAdjust / MaterialFPTAdjust (percent) */
    double peck_clear;      /* TlHolemakingOpParams.PeckClear */
    int is_rough, is_finish; double step_rxy, step_rz, step_fxy, step_fz;     /* TlMillingOpParams */
    char desc[128], mfg_code[64], chuck[64];    /* TlAssemblyItem.Description, TlToolMill.MfgToolCode / Chuck */
    char mat_name[32];      /* TlAssemblyItem.TlToolMaterialID -> TlMaterial.Name ("" = none) */
    int has_extras;         /* the fields above were read from a .tooldb / set by the wizard (else 0: not written back) */
    char holder[48];        /* holder name ("" = none) */
    double *holder_rz; int holder_n;   /* holder outline as (radius, height above gauge line) pairs */
    /* part-file TlMgr archive (../formats/part_tools.md; filled by part_tools.c, has_extras = 1) */
    int slot;               /* TlMgr slot: op_tool_info +0x264 = TL_ID ent +0x2cc (spec 1.1); -1 = not read */
    double true_len;        /* TlToolMill +0x870 TruePhysicalLength: the real tool length; oal is the stick-out (spec 5) */
    double stickout;        /* TlAssembly +0x90 Stickout (= oal in every part, spec 5) */
    char assembly[64];      /* TlAssembly TlObject Name */
    /* the tool's op_tool_info block as read (0x288 bytes: TL_ID ent +0x68, the TlTool level's raw block, a .tooldb
       TlTool.OpToolInfo of 648 bytes); TlTool::GetOpToolInfo starts from it (formats/mcam_write.md 8). 0 = none */
    int has_op_info; unsigned char op_info[0x288];
} Tool;

/* E_SPLINE (2026-10-26, formats/spline_chain.md): a parametric spline (record class 0x8) or NURBS curve (0x80); a / b = its stored
   start / end points (ent +0x68 / +0x80), crv = its CurveRec (F.crvs index: the record, evaluated by mcsurf.c). Chainable only for
   an Engrave operation (cam2d.c cam_chain_splines); every other user of Ent leaves it out or refuses it explicitly. */
/* E_SURF (2026-10-28, formats/surfaces_tab_plan.md): a surface entity (SURF_ID, class 0x100); srf = its F.surfs index. No polylines
   (npoly 0: its wireframe is the scene's, mcs_wireframe); picked / selected / deleted like the wireframe (core_ui), never chained. */
/* E_NOTE (2026-11-05, formats/drafting/notes.md): a note / label / leader-only entity (N_ID 0x10 / W_ID 0x20 record, v8_note body);
   nt = its F.notes index (Note: the record + the memory-table entries it references). No polylines (npoly 0: drawn by the drafting
   layer, scene.c draft_build); picked (draft_pick.c dp_distance), selected, deleted, undone, saved like the wireframe (core_ui), never
   chained, transformed, snapped to or analyzed (NOT PORTED for drafting). */
/* E_SCRV (2026-11-11, formats/wireframe.md "Curve Surface"): a surface curve (CONS_ID record, class 0x400: cons_type at +0x68, +0xd0 its
   surface's eptr - a trimmed surface's base -, +0xd8 its parameter curve's eptr: a 2D order-2 NURBS (0x80 with 2 values per point) that
   has no Ent of its own); crv = its F.crvs index (CurveRec class 0x400), a / b = the stored ends (+0x68 / +0x80). Drawn by mcam_cons_poly
   (mastercam.dll FUN_180237990, viewer/mccsurf.c), picked, selected, deleted, undone and saved like the wireframe; never chained,
   transformed, snapped to or analyzed (NOT PORTED for surface curves). */
/* E_MESH (2026-11-12, formats/cad_import.md 7): a polygonal mesh entity (PMESH_ID, class 0x100000), made by File > Open of an STL file
   (mcstl.dll DoImport -> mccore StoreSTLFileAsSTLMesh 0x1805ccac0) or read from a part; msh = its F.meshes index. No polylines (npoly 0:
   drawn by the scene, scene.c mesh layer); picked, selected, deleted, undone and saved like a surface (core_ui); never chained,
   transformed, snapped to or analyzed (NOT PORTED for meshes). */
enum { E_POINT = 1, E_LINE = 2, E_ARC = 4, E_SPLINE = 8, E_NOTE = 0x10, E_SURF = 0x100, E_SCRV = 0x400, E_MESH = 0x100000 };
typedef struct {            /* exact wireframe entity (for selection / chaining / CAM) */
    int type;               /* E_POINT / E_LINE / E_ARC / E_SPLINE */
    V3 a, b;                /* start / end (point: a = b) */
    V3 c; double r, a0, sw; /* arc: centre, radius, start angle, sweep (radians, XY plane) */
    int planar;             /* 1 if the entity lies in a plane parallel to XY (usable for 2D CAM) */
    int level, color;
    int first_poly, npoly;  /* its polylines in F.polys */
    int32_t idn, ctime;     /* file identity: ent_idn (+0x34) and created time (+0x30), for references from operations */
    int64_t eptr;           /* +0x20 */
    uint32_t cls;           /* record class (1 point, 2 line, 4 arc, 8 parametric spline, 0x80 NURBS curve) */
    int crv;                /* E_SPLINE: index of its record in F.crvs (CurveRec), else unused */
    int srf;                /* E_SURF: index of its record in F.surfs (Surf), else unused */
    int nt;                 /* E_NOTE: index of its record in F.notes (Note), else unused */
    int msh;                /* E_MESH: index of its record in F.meshes (McMesh), else unused */
    int64_t septr;          /* made here: its session eptr (references between new records; mcam_saveops.c surf_remap_refs), 0 = none */
    int created;            /* drawn in this program (written as new geometry); type 0 = deleted */
    int pt_style;           /* points: ent +0x1c point style 1..8 (mastercam.dll draw_3d_point_style); new points 1 */
    int ln_style;           /* ent +0x18 line style (set_gl_style: 1 solid, 2-7 stippled); new entities 1. Used by the
                               base drawing (scene.c dashed pass) and the dashed selection / highlight draw */
    int gen;                /* 1 = a curve made from a solid's edge for a solid chain (ps_edge_to_mc_curve; formats/chaining.md
                               "Solid chaining"): not geometry - not drawn (no polylines), not saved, never chained as wireframe */
    /* arcs in a view whose normal is not world +-Z (formats/mcam.md "Arc views"; a_3d +0x60 = A_ID +0xc8 view number):
       vw = that view number (>= 2), vm = its matrix rows (the view's x / y / z axes in world coordinates); c is the world
       centre (view_to_world of the stored view centre), a0 / sw are the stored view angles, and a point of the arc at
       angle t is c + r (cos t * vm[0..2] + sin t * vm[3..5]) (ps_arc_to_ps_arc / eval_curve). planar = 0 for them.
       vw = 0: an XY arc (c, a0, sw in world XY) as before. */
    int vw; double vm[9];
    int avw;                /* arcs read from a part: the record's view number (A_ID +0xc8) as stored, also for the +-Z views kept as XY arcs
                               (vw 0); 0 = not from a record (made here). Read by the Plane Selection's arc pick (core_ui cui_plsel.c) */
    int rec3c; uint16_t rec_sel;    /* ent +0x3c and +0x04 as read (FUN_18006d910, Create Letters: a record with +0x3c > 0 and bit 1 clear is skipped) */
    double width;           /* ent +0x10 line width (main_attrib +0; Home > Line Width, mastercam.dll FUN_18046a420 kind 2) */
    int blank_edited;       /* a part entity blanked / unblanked here (Break Many's Blank, cui_undo.c mod_blank): the save sets sel bit 0x100 from blanked */
    int attr_edited;        /* a part entity whose colour / level / style / width was changed here in place (Home > Attributes,
                               core_ui cui_attr.c): the save rewrites those record fields (mcam_saveops.c) */
    int blanked;            /* sel bit 0x100 (+0x04): blanked (ChangeBlankState, mccore 0x1806d72a0; draw_ent draws an entity only with the bit
                               off, mastercam.dll 0x1803312e0, decomp/surfdraw/drawent.c 52-54: `UnBlanking() != !(sel & 0x100)`):
                               kept and saved (the save sets the bit, mcam_saveops.c), its polylines flagged (Poly.blanked: not drawn, not
                               picked - select_ent_univ skips it, decomp/mastercam_seluniv.c 360-368), never chained; mcam_ent_blank */
} Ent;

typedef struct {            /* one op of a solid's history (McOperation base, formats/solid_history.md 2.3) */
    int type;               /* op type (factory FUN_180267440; name = opmanres 27601 + type, 2.2) */
    char name[64];          /* op +0x20, e.g. "Extrude" / "Extrude Cut" (HistDlg_addOp shows it, else the type name) */
    int id;                 /* op +0x34: 0, 1, 2 ... in history order */
    uint8_t f9, f10, f11, f12;   /* op +9 dirty, +0xa invalid, +0xb expanded, +0xc suppressed (mc_op_isDirty / isInvalid /
                               isExpanded / isDirectSuppressed, decomp/sldtree/sld_desc.c) */
    uint32_t x10;           /* op +0x10 (an op with it set is not editable: FUN_180264080) */
    size_t off, size;       /* its body (after the u32 type / u32 size) in Solid.ops_raw */
    size_t base_n;          /* bytes of the McOperation base at the start of the body (FUN_1802649f0); the class's own part follows
                               (SldToolPOp: viewer/sldtoolp.c) */
} SolidOp;

typedef struct {            /* embedded Parasolid solid (binary XT), see xt_*.c */
    char name[64];
    int ver;
    uint8_t *xt;            /* the .x_b bytes */
    size_t xt_size;
    uint8_t *brep; size_t brep_n;   /* an imported solid (STEP, viewer/cad_import.c): the OCCT BRep text; then xt = NULL, xt_size = 0 - occt_cad.h */
    int32_t idn;            /* ent_idn of its SOLID_ID entity record (+0xa8 there = N of the name "sld_N"); 0 if none */
    uint32_t sid;           /* the solid id N written after the XT */
    int level, color;       /* its SOLID_ID record's level (+0x08) and colour (+0x06) */
    int edited;             /* description / level / colour changed here (Analyze's solid dialog): written back on save */
    char desc[256];         /* the name after the id (0x40 / 0x100 bytes); sld_get_description = SOLID_ID +0xb8 = this */
    /* the ops (history) block = McOpsFile (formats/solid_history.md 2.1), kept byte for byte for the writer */
    uint8_t *ops_raw; size_t ops_n;
    int hist_ok;            /* 1 = the header and every op's base parsed (ops version >= 0x18), else only ops_raw is kept */
    uint8_t regen, expanded;     /* header +0x0c "needs regen", +0x0e = the solid row's expanded state (HistDlg_ExpandSolid,
                               mc_add_solid_to_dialog decomp/sldtree/sld_adddlg.c) */
    int32_t pos;            /* header +0x14: the history position (number of active ops; the "Stop Op" row goes after it) */
    int nops; SolidOp *ops;
} Solid;

typedef struct {            /* a surface entity: SURF_ID (class 0x100) record, formats/mcam.md "NURBS surface" / "Surfaces" */
    int32_t idn;            /* ent_idn (+0x34): what an op's drive references (TP kind 4, cls 0x100) name */
    int64_t eptr;           /* +0x20: what a trimmed surface's base reference (ent +0x100) holds */
    int level, color;       /* +0x08, +0x06 */
    int kind;               /* +0x68 u8: 0 NURBS, 1 parametric, 2, 4 loft, 6, 7, 8, 10 trimmed */
    int blanked;            /* sel bit 0x100 (+0x04): ChangeBlankState sets it to blank (hide) an entity (mccore 0x1806d72a0,
                               decomp/trimsrf/blank.c); a trimmed surface's base carries it */
    uint8_t *rec; uint32_t n;   /* the record as read (the kind's data follows the 0x98-byte surf header at +0x68) */
    int ent;                /* its Ent (E_SURF) in F.ents, -1 = none */
    int off;                /* its Ent is deleted / hidden here (core_ui ent_off): not drawn, not picked */
    int edited;             /* the record was changed here (e.g. the normal flipped): written back on save */
} Surf;

/* a polygonal mesh entity (PMESH_ID, class 0x100000; formats/cad_import.md 7). The record (mccore bytes_per_ent 0x1802e5230: 200 + 0x68
   bytes in memory) = the ent header, +0x68 i16 kind (FUN_1805cc510 / StorePolygonalMeshDataFromStream: 0 STL, 2 IFF, 3 MCO), +0x70 i64
   the UtMMFDataMgr entry (in memory only: the writer FUN_1802c72c0 drops these 8 bytes, FUN_1802c2f00, and the reader puts -1 back,
   FUN_1802c28d0), +0x78 the 4x4 matrix (16 doubles, row major: MtTmat3d of MCLoadSTLMesh::GetSTLPoly), +0xf8 / +0x110 the box min /
   max (FUN_1805cc560), +0x128 u8 the box is set. On disk the record is followed by the entry's data (u32 length = 0x128 + its size).
   STL data (kind 0, UtMMFDataMgr enhanced form, the .data byte DAT_181311e62 = 1): the triangles as RawSTLPoly (CreateSTLEntryFromStream). */
typedef struct { float n[3], v[9]; uint8_t attr; } StlPoly;   /* RawSTLPoly: the facet normal, the three vertices, the attribute's low byte (+0x30) */
typedef struct {
    uint8_t rec[0x130];     /* the record as in memory (+0x70 = -1) */
    int kind;               /* +0x68 */
    StlPoly *p; int np;     /* kind 0: the triangles in their stored order */
    int mixed;              /* kind 0 from a binary STL whose attribute bytes differ (WriteEnhancedSTL sorts them: NOT PORTED, the save refuses) */
    int32_t idn; int64_t eptr; int level, color, blanked;
    int ent;                /* its Ent (E_MESH) in F.ents */
    int off;                /* its Ent is deleted here (core_ui ent_off): not drawn, not picked */
} McMesh;
/* UtMMFDataMgr::CreateSTLEntryFromStream (mccore 0x1808c1610) for a binary STL stream of N bytes (the data of a part's PMESH record,
   StorePolygonalMeshDataFromStream kind 0, and a binary .stl file): 0x54-byte header, N must be count * 0x32 + 0x54; each facet's
   normal normalised (zero when its squared length <= 1e-12); *mixed = an attribute byte differs from the first. Returns the count, -1 bad. */
int mcam_stl_entry_bin(const uint8_t *b, size_t n, StlPoly **out, int *mixed);
/* UtMMFDataMgr::WriteEntryToStream (mccore 0x1808c3060) of an STL entry in the enhanced form: the 0x50-byte header "MASTERCAM STL DATA"
   (+ spaces, .data 0x181306720), the count, per facet 0x32 bytes (the first vertex's normal, the vertices, the run's colour byte, 0).
   NULL when the mesh is mixed (the run order needs WriteEnhancedSTL's std::sort: NOT PORTED). */
uint8_t *mcam_stl_entry_write(const McMesh *m, size_t *n);

/* a memory-table entry (DB_ID 0xE block, mTableRead core_draft_rec2.c:2416-2497; formats/drafting/records.md 0): the id the record
   references (a file-local token: mTableRead maps every stored id to a new one, FUN_1802fcff0) and the data */
typedef struct { int32_t id; uint8_t *p; uint32_t n; } NoteMt;
typedef struct {            /* a note / label / leader (E_NOTE): N_ID 0x10 / W_ID 0x20 record, formats/drafting/records.md 3 */
    uint8_t E[0x1c8];       /* the record (ent bytes: 0x68 header + 0x160 v8_note body); fixed size (bytes_per_ent) */
    NoteMt *mt; int nmt;    /* the memory-table entries it references: the text (+0xf8, kind > 1), the leader's aPoint array (wl +0x20
                               when wl +0x19), the chained leaders (wl +0x84), recursively. Ids unique in the McamFile (part: the
                               file's; made here: McamFile.mt_next) - the save gives new notes file ids of their own */
    int ent;                /* its Ent (E_NOTE) in F.ents */
} Note;

typedef struct {            /* a curve entity's record (point 1, line 2, arc 4, parametric spline 8, NURBS curve 0x80) by its eptr:
                               what surfaces reference (revolved / swept profiles; mcsurf.c eval_curve) */
    int64_t eptr; uint32_t cls; uint8_t *rec; uint32_t n;
} CurveRec;

typedef struct {
    V3 *pts; int npts, cappts;
    Poly *polys; int npolys, cappolys;
    Op *ops; int nops;
    Ent *ents; int nents;
    Tool tools[256]; int ntools;
    Solid *solids; int nsolids;
    struct SldNew *sldnew; int nsldnew;   /* solids made here to write at the next save (core_ui cui_sldsave.c sets them, sld_ops.h; not owned) */
    Surf *surfs; int nsurfs;
    McMesh *meshes; int nmeshes;   /* polygonal meshes (PMESH_ID, E_MESH) */
    CurveRec *crvs; int ncrvs;
    int nfviews; struct { int n; double m[9]; int isnew; } *fviews;   /* the part's stored views (DB_ID subtype 0: number + matrix rows) */
    int32_t (*sld_ids)[4]; int nsld_ids;   /* SOLID_ID records: ent_idn (+0x34), solid id N (+0xa8), level (+0x08), colour (+0x06) */
    TpRef *tp; int ntp;     /* geometry references of the operations, in file order */
    size_t nci_off;         /* file offset of the first binary NCI record (0 = none) */
    int major;              /* file version: header +0x6a (0x12 X9 / .mcx-9, 0x13 2017 / .mcam) */
    int metric;             /* part units: file header +0x6f (1 mm, 0 inch; mccore FUN_1802ae550), else the NCI (1016 +0x44 met_tool): 2 mixed, -1 unknown;
                               the UI sets it to the system units after a units change (cui_units_set) - read it through mcam_is_metric */
    int nci_metric;         /* the stored NCI's units (1016 +0x44 met_tool, written with IsMetric() when Mastercam made it): 1 / 0, 2 mixed, -1 none */
    long nlines, narcs, npoints, nmoves, nnciarcs, ndrill, arc_fixups;
    V3 lo, hi;              /* extents (all points) */
    double stock[3];        /* the active (last) machine group's stock size: op_group +0x1230 / +0x1238 / +0x1240 (0 = none) */
    double stock_org[3];    /* Stock Setup origin point: op_group +0x1248 / +0x1250 / +0x1258 */
    int stock_sel;          /* which box corner/edge the origin marks: op_group +0x1690 (job_setup_stock_corners);
                               0 = centre of the top face, 1-8 the corners (formats/verify.md) */
    /* the rest of the machine group's Stock Setup (struct group_pg3 = op_group +0x1228, mastercam.dll CGroupPg3 dialog
       1590 / mccore stock_ok / stock_calc_geom / draw_stock; formats/mcam.md "Stock Setup"): +0x1228 Fit screen, +0x1260
       Display, +0x1261 shaded (the Wire frame / Solid radio), +0x1264 shape (0 rectangular, 1 cylindrical, 2 solid, 3 file),
       +0x1268 solid ent id, +0x126c cylinder axis, +0x1270 centre on axis, +0x1271 STL file, +0x16a8 Use Machine Tree;
       +0x8a8 the stock plane (group_view: i16 plane id, i16 view number, +8 matrix_33, +0x50 origin); +0x1380 the 12 edge
       lines stock_calc_geom stores (world). stock_rec = 1 when a machine group record was read; stock_dirty = changed
       here (the save writes the fields back, mcam_saveops.c) */
    int stock_grp;          /* the active machine group's id (op_group +0x0; the last machine group, COpMgrTree::LoadTree):
                               ops with record +0x5bc == it belong to it */
    int stock_rec, stock_fit, stock_disp, stock_shaded, stock_shape, stock_solid, stock_axis, stock_center, stock_mtree, stock_dirty;
    char stock_file[0x104];
    int stock_vid, stock_vno; double stock_vm[9], stock_vo[3];
    int stock_plane_dirty;
    /* the part's MastercamPlanes archive (ASSOC 36; formats/planes_archive.md, mcam_load.c planes_archive): planes_ok = read to its
       exact end; planes_maxcls / planes_hi2 = the highest PlaneID class and the highest id of class 2 (named planes) - what
       MastercamPlanes::GetHighID(2) looks at; planes_n = planes read */
    int planes_ok, planes_maxcls, planes_hi2, planes_n;
    int planes_act[4], planes_org[4];   /* SystemPlanes +0x110 / +0x120: the active plane / origin id per PlaneType (0 Cplane, 1 Tplane, 2 Gview, 3 WCS; TypeToInt = identity: formats/bbox_toolplane.md) */
    double planes_um[9], planes_uo[3];  /* SystemPlanes +0x10 / +0xe0: the unnamed matrix / origin (GetUnnamedM33 0x1800a96c0 / GetUnnamedOrigin 0x1800a96d0): what an active plane / origin id < 1 reads (MastercamPlanes::GetActivePlane 0x1802c5e30, GetActiveOrigin 0x1802c5830) */
    struct McamPlaneInfo *planes_list; int nplanes_list;   /* the archive's planes in archive order (core_ui cui_planes.c planes_restore_part) */
    /* the part's view record, 0x200 sub 1 (written by save_db mccore 0x1802bc3d0, applied by store_db_ent 0x1802c5dd0 while reading, before the
       MastercamPlanes record): +0x6c Gview number (-1 = the mouse view), +0xa0 the window centre (p_2d), +0xb0 the construction
       depth, +0xb8 the window height. view_mat = the matrix of view number gv: the reserved views 1..8 or the part's sub 0 record */
    int view_rec, view_gv, view_mat_ok; double view_mat[9], view_center[2], view_height, constr_depth;
    int stock_gtype;        /* op_group +0x19e8: the machine group's type (0 mill; 1 lathe / 6 mill-turn: stock_ok is 0 for them) */  /* the stock plane was chosen here (Stock Setup 960): the save writes the group_view at +0x8a8 */
    double stock_lines[12][6];
    /* cylindrical stock (shape 1, stock_calc_geom 0x1800d5d80): the two end circles as a_3d at +0x15c0 / +0x1628 - centre in
       the axis view's coordinates (+0x30), radius = size X / 2 (+0x48), start 0 (+0x50), sweep 2 pi (+0x58), view (+0x60) */
    double stock_arc[2][3]; double stock_arc_r; int stock_arc_view;
    /* the control definition's mill tool registers (first ASSOC 24 control page 9 = NC path, the page CCNCControl::
       AddNcPath writes from control +0x238 at ent +0x80; GetToolMT's nc_path_tool_ctrl at control +0x456 = ent +0x29e):
       +4 add to tool # for length offsets, +6 for diameter offsets (CCNCMachine::LOfsAmt / DOfsAmt), +0xc use head
       (UseHeadForTool); ctl_found 0 = no control page in the file */
    int ctl_found, ctl_lofs, ctl_dofs, ctl_head;
    int ctl_addreg;         /* +0 of that struct: CCNCMachine::AddToToolRegister(1) (mcmachinedef @180071e30 = CtrlToolInfo(1) u16 +0,
                               decomp/mdef_addtotoolregister.c; 0 in every user part and both defaults libraries) */
    /* the active (last) machine group's tool settings (op_group +0x1178, formats/feeds_speeds.md): Feed Calculation mode (0 tool,
       1 material, 2 defaults, 3 user defined), material name, user defined plunge / retract / feed / rpm; the
       machine definition's material library path (machine record 0x4000 sub 22 = CNC_MACHINE_ID, +0xce7; mcam_load.c) */
    int grp_fs_mode; char grp_material[0x52]; double grp_plunge, grp_retract, grp_feed, grp_rpm; int have_grp;
    char matlib[256];
    /* the same machine record's tool libraries, CCNCMachine::ToolLibFile(units) (mcmachinedef @18007bb00: machine +0xb26 inch,
       +0xc2b metric, 0x104 chars) at record +0xab6 / +0xbbb, and CCNCMachine::Type (+0xd8 = record +0x68: mill 1, router 3;
       HasAlternateIndexPositionTurret @180069da0 needs 2): record = machine - 0x70, measured on the shipped Mill / Router
       defaults files and the user's parts (the fields read "%MCAMDIR%Mill\Tools\Mill_mm.Tooldb"; the material field is at
       +0xcc0, matlib's +0xce7 is a second string after its NUL). mach_ok 0 = no machine record */
    char toollib[2][0x105]; int mach_type, mach_ok;
    /* The active machine group's machine and control (2026-10-23, formats/grpprops_tabs.md 2.2): the machine record is the
       one whose ent id (+0x34) is the active group's +0x1170 (CGroupPg0::UpdateGroupAndDialogMachines; measured), its control
       the CNC_CONTROL_ID record (ASSOC sub 23) whose ent id is the machine record's +0xe94 (CCNCMachine::Ctrl_ent_idn =
       machine +0xf04, mdef_grpprops_accessors.c), the control's pages the sub 24 records with ent id = control id + 1 + page
       and page number +0x68 = page (measured; the stored page -> control link field is NOT DECODED). matlib / toollib /
       mach_type and ctl_* above come from that chain (mcam_load.c resolve_machine); with no group record or no match: the
       first machine record / the first control page 9 in the file (ours, the loader's earlier rule). */
    int mach_id;            /* the machine record's ent id (0 none) */
    char mach_comment[0xd0], mach_ctrl[0x105], mach_post[0x105];   /* CCNCMachine::Comment / CtrlDefFile / PostProcFile: record +0xdc5 / +0x8ac / +0x9b1 */
    float mach_minarc[2];   /* Dynamics +0x30 / +0x34 (inch / metric) = record +0xf8 / +0xfc: MinArcFeedRate (FUN_1801ec2d0) */
    int ctl_id, ctl_ok;     /* the control's ent id; 1 = its pages 2 / 8 / 9 were read */
    /* batch 11: the machine's coolant definition (CCNCMachine::Coolant() = object +0x298 = record +0x228, record = object - 0x70 by
       CCNCMachine::Type +0xd8 = record +0x68; mcmachinedef.dll CoolantBaseTxt / CoolantOnTxt / CoolantOffTxt / CoolantButtonTxt /
       UseCoolantFromPost, ~/mastercam-re/work/b11/mdef_cool.c): 10 base texts of 0x50 (+0), on texts of 0x28 (+0x320), off texts of
       0x28 (+0x4b0), the button text (+0x640), UseCoolantFromPost (+0x669, 1 = V9 style: the op's coolant word op +0x330). cool_ok = 1
       when read */
    int mach_cool_ok, mach_cool_frompost;
    char mach_cool_base[10][0x51], mach_cool_on[10][0x29], mach_cool_off[10][0x29], mach_cool_btn[0x29];
    int ctl_pgfb;           /* bit p: page p found only by the file-order fallback (resolve_machine; reported by gpdump) */
    char ctl_ncpath[0x105], ctl_oplib[2][0x105], ctl_opdef[2][0x105];   /* page 2 (file_ctrl): NC path ent +0x94, OpLibFile +0x4a4 / +0x5a8, OpDfltsFile +0x6ac / +0x7b0 */
    char ctl_ncsuffix[0x105]; /* page 2 file_ctrl +0xc36 = ent +0xcc4: CCNCMachine::NCSuffix (mcmachinedef @180077080, file_ctrl = ent +0x8e as
                                 NCPrefix +6 = ent +0x94 / OpDfltsFile +0x61e = ent +0x6ac show); "" = not read (nci_write.c, Q24 naming) */
    double ctl_seq[3];      /* page 8 nc_output_ctrl (ent +0x80): +0 StartSeqNum, +8 SeqNumInc, +0x10 the maximum */
    int ctl_ncout[4];       /* nc_output_ctrl +0x129 op comments, +0x12a group comments, +0x12b group name, +0x12d machine name */
    int ctl_arcfeed;        /* page 9 ent +0xa35: the mill NC path feed ctrl +0xd (AdjustArcFeedrates(1)) */
    int ctl_post_ok; unsigned char ctl_post[15];   /* the control's post_dlg_settings (CCNCControl::GetPostSettings +0x416b..+0x4179 = file_ctrl
                               +0xd3b = page-2 ent +0xdc9; formats/align_geo.md 4.1): [0] NCI keep, [1] NCI overwrite, [2] NC keep, [3] NC
                               overwrite (0 = Ask), [8] NC edit. Read for 2017 parts (major 0x13); X9: NOT DECODED (ctl_post_ok 0) */
    /* the active machine group record's Files / Tool Settings fields (op_group, formats/grpprops_tabs.md 2 / 3): +4 name,
       +0x355 machine definition file, +0x980 comment, +0x1170 machine ent id, +0x1178 program #, +0x115c..+0x1168
       override with modal values / clearance / retract / feed plane, +0x11b8..+0x11c4 assign tool numbers sequentially /
       warn of duplicates / use tool's step, peck, coolant / search the tool library; grp_rec = 1 when read. grp_dirty =
       changed here (mcam_saveops.c grp_patch writes them back). grp_names = every group's name (TpGrpList::IndexByName). */
    int grp_rec, grp_dirty, grp_id, grp_mach;
    char grp_name[0x105], grp_mdef[0x105], grp_comment[2000];
    int grp_prog, grp_ovr[4], grp_tcfg[4];
    char (*grp_names)[0x105]; int ngrp_names;
    int grp_mppost;         /* ours (not in the part): the machine group's "Post with Mastercam post (.pst)" switch, 0 = the LinuxCNC
                               post (nc_post.c), 1 = the group's .pst (mach_post) through viewer/mp (user decision P2; Files page,
                               core_ui cui_grpprops.c; kept in the project file, GRPMPPOST line) */
    long long file_size, file_mtime;   /* of PATH when loaded: the post re-reads the stored NCI from it */
    /* drafting entities made here (core_ui cui_draft.c): the D_ID record image (ent 0x68 header + 0x7a8 body) as Mastercam's
       calc_linear_dimension leaves its drag entity; ent[k] = the entity aPoint k references (index into ents, -1 none: the eptr is
       written at save time, mcam_saveops.c); on = 0 after an Undo */
    struct DraftNew { uint8_t E[0x810]; int ent[2]; int on; } *dnew; int ndnew;
    /* notes / labels / leaders (E_NOTE; formats/drafting/notes.md): the part's N_ID / W_ID records with their memory-table
       entries, then the ones made here (draft_note.h mcam_note_add). mt_next = the next memory-table id for a note made here
       (above every id of the part's table) */
    Note *notes; int nnotes; int32_t mt_next;
    /* the level table (mastercam.dll LevelData, decomp/levels/mastercam_leveldata.c): nodes in creation order - level, name
       (" " = no name, as LevelNode's ctor), visible. Built at load from the system record DB_ID 0x200 subtype 0x15 (mccore
       FUN_1802d5220, decomp/levels/core_levels.c: +0x7c visible count, +0x80 named count, then the visible levels (i32) and the
       named ones (i32 level + char[0x130] name)) and the levels of the records after it (FUN_1802c4010: a missing level is made
       hidden while a file is read); a level made here is visible (FUN_1802c4010 outside a load). lvl_dirty: rewrite the record
       on save (save_db, formats/levels.md) */
    struct McLevel { int32_t level; char name[0x134]; uint8_t vis; } *lvls; int nlvls; int lvl_dirty;
    int main_level;           /* the level record's main level (FUN_1802d5220 set_main_level), 1 without a record (dbinit) */
    /* lathe stock (formats/lathe.md 7.2): the LBDRY_ID records (ASSOC subtype 10 = struct lathe_bdry at ent +0x68: +0 i32 entity count,
       +4 u8 boundary index (0 left / 2 right stock ...), +5 u8 valid, +8 i32 owner op id (0x7fffffff = a machine group's), +0x28 f64
       1000 (BOUNDARYCLASS::GetFrom_lbdry, mcmachinedef 0x1800346d0), +0x90 the tp_ent entities, 0x78 bytes each), by ent id; and every
       group's six component boundary ids (op_group +0x16ac / +0x16b4 / +0x16bc / +0x16c4 / +0x16cc / +0x16d4 =
       LATHEPROJECTCLASS::GetComponentsFromDb 0x18010d120 local_78[0..5]), its parent (+0x10c) and type (+0x19e8: 1 lathe) */
    struct McLbdry { int32_t idn; uint8_t *rec; uint32_t n; } *lbdry; int nlbdry;
    struct McGrpLathe { int32_t id, parent; int gtype; int32_t comp[6]; uint8_t g1698[8], g16a0[8]; int32_t g11c0;
                        int32_t g119c;   /* op_group +0x119c: the tool page's feed / speed source (ToolPrmBase::GetToolFeedInfo 0x1801b37b0: 0 the tool, 1 material, 2 defaults, 3 user defined) */
                        double g11a0, g11b0;   /* op_group +0x11a0 feed / +0x11b0 spindle speed ("User defined", formats/grpprops_tabs.md) */
                      } *lgrp; int nlgrp;
    int32_t *lgrp_mach;     /* lgrp[k]'s op_group +0x1170: its machine record's ent id (lathe_axis.c: OMgetOpMachine) */
    /* the part's lathe tools (LTL_ID: ASSOC subtype 8, LATHETOOL body = ent +0x68, 0xa48 bytes; LATHETOOLCLASS::SetInfo 0x180158390
       copies it to this+8), in file order */
    struct McLtl { int32_t idn; uint8_t *body; uint32_t n; } *ltl; int nltl;
    /* machine-definition records (lathe_axis.c, formats/lathe.md 8) and the part's materials (sub 4, body 0x3a0; lathe.md 11): ASSOC subtypes 12 component, 13 component group, 15 id list,
       21 component-group params, 22 machine, 26 axis combination; body = ent +0x68, in file order */
    struct McMdRec { int sub; int32_t idn; uint8_t *body; uint32_t n; } *mdrec; int nmdrec;
    char path[1024];
} McamFile;

int  mcam_load(McamFile *f, const char *path);   /* 1 = ok */
/* the level table: the node of LEVEL (NULL if none); LevelData::setLevel(level, vis, 1) / setName (a missing level is appended,
   visible as LevelNode's ctor) / getName (" " when none); mccore set_levname @1803cfc10: at most 300 characters, leading /
   trailing blanks removed (del_lt_spaces), then setName; a named level = name not " " and not empty */
struct McLevel *mcam_level(McamFile *f, int level);
void mcam_level_set(McamFile *f, int level, int vis);
void mcam_level_setname(McamFile *f, int level, const char *name);
void mcam_set_levname(McamFile *f, int level, const char *name);
int  mcam_level_named(const struct McLevel *l);
/* LevelData::isVisible @180245fc0 (decomp/mastercam_hidezoom.c 913-926): the level node's visible flag, 0 for a level not in the
   list. Mastercam draws an entity only on a visible level (draw_ent @1803312e0, decomp/surfdraw/drawent.c 116 / 144: the
   display-list store store_ent_dl too; surfaces FUN_18021e020 decomp/surfdraw/srfdraw.c 16; notes / drafting
   decomp/draft/core_draft_rec.c 1324), and its picks, window selection, AutoCursor and fit walk that display list
   (select_ent_univ FUN_18020f4e0 decomp/mastercam_seluniv.c 300-353, the window selection decomp/xform/mastercam_dbselwin.c
   317-330, dl_search_fast decomp/mastercam_dlsearch.c 132, the fit FUN_180320770 decomp/mastercam_fit2.c 455-470) */
int  mcam_level_visible(const McamFile *f, int level);
/* FUN_1802c4010 (store_ent / write_ent's level bookkeeping) outside a load: an entity written to a level not in the list makes it,
   visible (the same rule as a new entity, mcam_load.c add_ent) - for a level changed in place (Change Level, Analyze) */
void mcam_level_store(McamFile *f, int level);
/* XfBlankEntity / XfUnblankEntity (mccore; ChangeBlankState 0x1806d72a0): the entity's sel bit 0x100 on / off, its polylines with it */
void mcam_ent_blank(McamFile *f, int e, int blank);
extern int mc_main_level_vis;   /* mccore main_level_vis: the main level kept visible (the Level Manager's check box); 0 after a load
                                   whose main level is not in the record's visible list (FUN_1802d5220) */
/* the planes of a MastercamPlanes archive (ASSOC 36 record + 0x68; formats/planes_archive.md): PlaneID class / id, PlaneBase +0x110 name,
   Plane +0x358 (second CString) and +0x360 (the bool after it). Returns the count listed, -1 when the archive is not read to its end */
typedef struct McamPlaneInfo { int cls, id, is_plane, b360; char name[0x80], s358[0x80];
    double m[9], org[3];      /* PlaneBase +0x40 matrix rows; Plane +0x130 origin (PlanePoint +0x10, GetPointInWorldCoordinates 0x1800a9270, world coordinates; a class 1 PlaneBase has none: 0) */
    int wofs, disp, coord, ptype; int b118; } McamPlaneInfo;   /* b118: PlaneBase +0x118 bool (formats/planes_archive.md; LatheACTplanes::ACSupportsToolplane skips a plane with it set) */
/* Plane +0x330 work offset, +0x334 Display, +0x348 Coordinate, +0x368 PlaneType (Plane::Serialize 0x1803ddb80, PlanesMgrBar::SetPlanesListIcons 0x180412640) */
int mcam_planes_list(const uint8_t *archive, size_t n, McamPlaneInfo *out, int max);
void mcam_free(McamFile *f);
double mcam_tool_dia(const McamFile *f, int num);
const char *mcam_error(void);                          /* why the last mcam_load failed */
enum { MCAM_ERR_NONE, MCAM_ERR_OTHER, MCAM_ERR_NEWER };
int mcam_error_code(void);                           /* the last mcam_load: MCAM_ERR_NEWER = a part from a newer release than Mastercam 2017 (mcam_grp.h
                                                        mcam_part_newer; mcam_error() is then mccoreres 5280 verbatim), MCAM_ERR_OTHER = any other failure */
const Surf *mcam_surf_base(const McamFile *f, const Surf *s);   /* a trimmed surface's base (kind 10: ent +0x100 = its eptr), else NULL */
const Surf *mcam_surf_ref(const McamFile *f, int64_t eptr);   /* the surface a reference names: its eptr, or the session eptr of one made here (Ent.septr: references made before a save keep it) */
const Tool *mcam_tool(const McamFile *f, int num);     /* last tool with that number, or NULL */
const char *mcam_opcode_name(int code);

typedef struct { int rapid; V3 p; double feed;              /* generated toolpath move (feed in units/min) */
                 int arc, cw; double cx, cy;
                 int cc;
                 int apl; double cz;
                 int ctf; } Move;   /* ctf: the NCI contour flags of the move's record (lin +0x40 / arc +0x58: 1 first, 2 last move of a
                                      contour pass, gr_lin_mill / gr_arc_mill's flags argument) - set by the Thread Mill engine (cam2d_ops.c
                                      tm_pass, FUN_180236810), 0 elsewhere */
                                   /* apl: the arc's plane 0 XY, 1 YZ, 2 XZ (Mastercam NCI planes; 1 / 2 only from the arc
                                      filter with MCAM_ARC_FILTER_PLANES=1, cam_filter.c), centre (cx, cy, cz) world, cw in the
                                      plane's (u, v) = (y, z) / (x, z) frame (nc_post.c AX) */   /* cc: cutter compensation in control on this move - 41 / 42 start (G41 / G42), 140 end
                                      (G40), 0 none: the contour NCI writer FUN_180056be0 (decomp/mcmill_all.c l.59875) via mccore
                                      cc_control_gcode_2; batch 5, posted only with MCAM_CUTTER_COMP=1 (nc_post.c) */         /* arc > 0: this chord belongs to the engine's arc record number
                                                                arc (consecutive moves with the same number = one arc from the
                                                                point before them, centre cx cy, cw; Z linear = helix); 0 = a
                                                                line. Display code may ignore it (the chords are the path). */
/* append a generated operation (moves are polylines; returns the new op index, ops keep Operations Manager order) */
void mcam_remove_op(McamFile *f, int index);
void mcam_move_op(McamFile *f, int from, int to);          /* reorder F.ops (the operation keeps its id) */
/* geometry editing (File > New parts, Wireframe commands) */
void mcam_new_empty(McamFile *f);                           /* an empty part (like Mastercam's File > New) */
/* units (formats/units.md). mccore keeps ONE units flag, IsMetric() (@1805fa070: the byte DAT_18142d400), set by SetUnits
   (@1805fa8c0) from the current configuration file (item 1 "File is metric?"); every units-dependent value reads it, and a
   saved part is stamped with it (version entity +0x6f, every record +0x4d: FUN_1808c4ce0 / FUN_1802c5a00). Opening a part
   whose header units (+0x6f, < 2) differ switches the configuration to them (ChangeConfiguration, ProcessVersionEntity
   FUN_1802d5f60); File > New keeps them (init_db). mc_units_metric = that flag (1 mm, 0 inch); the program starts metric
   (ours: the startup configuration mcamxm.config, formats/ui.md "File > New"). */
extern int mc_units_metric;
int mcam_is_metric(const McamFile *f);   /* IsMetric() for part F: its header units (0 / 1), else (-1 unknown, 2 mixed NCI) mc_units_metric */
/* Mastercam's system tolerances (formats/units.md 8): the globals of the current configuration's [TOLERANCES] items
   1000-1008 (ReadCfgFile FUN_1801bb2c0 -> FUN_1801bab70: 1000 tol, 1001 chain_tol, 1002 chain_plane_tol, 1003 min_arc_length,
   1004 curve_min_step, 1005 curve_max_step, 1006 curve_chord_dev, 1007 surf_step_chord_dev, 1008 nc_tol; then FUN_1801b6bd0
   raises them to tol / nc_tol), plus the shading chord height gl_tolerance (item 7004, FUN_1801ba530). Defaults (no item):
   mccore FUN_1803bc7d0 / mastercam.dll FUN_1801b4070 for the units, applied once at startup (FUN_1803b9f90); a units change
   (ChangeConfiguration) reads the new configuration over the values (an absent item keeps its value). decomp/mccore_systol.c,
   decomp/units/core_cfgcallers.c, core_cfgmisc.c, systol_readers.c. One set, as in Mastercam: the engines, chaining and the
   front end all read it; mc_systol_units(IsMetric()) re-reads it when the units differ from the ones it holds. */
typedef struct {
    double tol, chain_tol, chain_plane_tol, min_arc_length, curve_min_step, curve_max_step, curve_chord_dev,
           surf_step_chord_dev, nc_tol;   /* config 1000 .. 1008 */
    double gl_tolerance;                  /* config 7004 Chord height (shading) */
} McSysTol;
extern McSysTol mc_systol;
/* defined in mcam_saveops.c (with the configuration reader): programs linking it only */
void mc_systol_defaults(int metric);          /* FUN_1803bc7d0 + FUN_1801b4070's gl_tolerance */
void mc_systol_item(int key, double v);       /* FUN_1801bab70 (1000-1008) / FUN_1801ba530 (7004) */
void mc_systol_check(void);                   /* FUN_1801b6bd0 */
const McSysTol *mc_systol_units(int metric);  /* the values for IsMetric() = METRIC: the first call = startup (defaults + the
                                                 configuration), a change of units = ChangeConfiguration's ReadCfgFile */
int mc_systol_held(void);                     /* the units the values were read for (-1: none yet) */
int mcam_new_ent(McamFile *f, int type, V3 a, V3 b, V3 c, double r, double a0, double sw, int level, int color);   /* returns index */
int mcam_surf_ref_offsets(const uint8_t *rec, uint32_t n, uint32_t *offs);   /* the eptr fields referencing other entities (<= 8) */
int64_t mcam_session_eptr(void);                     /* a stand-in eptr for an entity made here (Ent.septr) */
int mcam_new_note_ent(McamFile *f, int k);           /* the E_NOTE Ent of F.notes[k] made here (draft_note.c); returns the Ent index */
int mcam_new_surf(McamFile *f, const uint8_t *rec, uint32_t n, int level, int color);
/* the surface a reference names now: tp_substitute_ent's target while it is in force (TpRef.subst1), else -1 */
static inline int tp_ref_subst(const McamFile *f, const TpRef *t) {
    if (!t->subst1 || t->subst1 > f->nsurfs) return -1;
    const Surf *ns = &f->surfs[t->subst1 - 1]; if (ns->off) return -1;
    for (int k = 0; k < f->nsurfs; k++) if (f->surfs[k].idn == t->ent && t->ent && !f->surfs[k].off && !f->surfs[k].blanked) return -1;   /* the named surface lives (an undo): not in
                                                   force; blanked counts as gone (Surfaces > Offset's Move: delete_ent(.., 1, 1) blanks the original) */
    return t->subst1 - 1;
}   /* a new E_SURF from a SURF_ID record; returns the Ent index */
/* a new E_MESH (StoreSTLFileAsSTLMesh): REC = the 0x130-byte record, P = its NP triangles (taken over). Returns the Ent index. */
int mcam_new_mesh(McamFile *f, const uint8_t *rec, StlPoly *p, int np, int mixed, int level, int color);
int mcam_add_view(McamFile *f, const double M[9], double tol);   /* AddMCView: the number of the view with this matrix, a new one when none matches */
int mcam_new_curve(McamFile *f, const uint8_t *rec, uint32_t n, int level, int color);   /* a new E_SPLINE from a class 8 / 0x80 record; -1 bad */
/* a new surface curve (E_SCRV, Curve Surface): body = the CONS body (0x88 bytes, viewer/mccsurf.c CsResult), its uv points (the order-2
   parameter curve's control points), the surface it lies on (an E_SURF Ent: a trimmed surface's base is referenced); -1 bad */
int mcam_new_cons(McamFile *f, const uint8_t *body, const double (*uv)[2], int npts, int surf_ent, int level, int color);
/* the display polyline of a surface curve (set by viewer/mccsurf.c: programs without it leave surface curves undrawn) */
extern int (*mcam_cons_poly)(const uint8_t *cons, uint32_t ncons, const uint8_t *uv, uint32_t nuv, const uint8_t *srec, uint32_t sn, const uint8_t *base, uint32_t bn, double (**pts)[3]);
/* a new arc in view VIEW (2..8 a reserved view, M its rows; formats/mcam.md "Arc views"): CV = the centre in view
   coordinates, a0 / sw view angles. A view whose normal is world +Z makes an XY arc (vw 0), as the loader does. */
int mcam_new_arc_view(McamFile *f, int view, const double M[9], V3 cv, double r, double a0, double sw, int level, int color);
int mcam_new_arc_view_w(McamFile *f, int view, const double M[9], V3 cw, double r, double a0, double sw, int level, int color);   /* cw = world centre */
V3 mcam_arc_pt(const Ent *e, double ang);                   /* the world point of an arc at angle ang (its own frame) */
int mcam_std_view(int n, double m[9]);                       /* reserved view n (1 Top .. 8 Axonometric) rows; 0 if none */
void mcam_delete_ent(McamFile *f, int i);                   /* type 0, its polylines emptied */
void mcam_update_extents(McamFile *f);               /* removes F.ops[index] and its polylines */
int mcam_add_op(McamFile *f, const char *type, const char *comment, int tool, const Move *m, int n);
/* replace F.ops[index]'s NCI section (takes SEC, malloc'd) and rebuild its toolpath polylines from it */
int mcam_op_nci_section(const McamFile *f, int index, uint8_t **sec, size_t *n);   /* the stored op's binary NCI section (malloc); 1 = ok */
void mcam_op_set_nci(McamFile *f, int index, uint8_t *sec, size_t n);
/* the X9 (file major 0x12) -> 2017 op record update Mastercam applies as it reads an X9 part (mcprvverfile.dll FUN_18005e7f0) on
   E (an ASSOC 1 record, 0x2938 bytes: ent + op); METRIC = the part's units. 0 = not decoded for this opcode (nothing changed) */
int mcam_x9_op_update(uint8_t *e, int metric);

#endif
