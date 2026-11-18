/* cui_plsel.c - the Plane Selection dialog: mccore get_plane_info (0x180a2aea0) and its dialog CPlaneDlg (mccoreres dialog 1085
 * "Plane Selection"). Spec: ../../formats/plane_selection.md. Dumps (gitignored): decomp/planesel/get_plane_info.c (the function,
 * 1606 lines), gpi.c (its callees), dlg_range.c (CPlaneDlg 0x180238130-0x180239800), msgmap.txt (the message map at 0x180d320c0),
 * small.asm (the one-line handlers), selcue*.c / mcreal_core.c (SelectionCue -> MCReal::SelcueEnd), arrow.c (draw_plane_arrow),
 * sld_planar.c (mcgeomsld sld_is_surf_planar / sld_check_face_normal), cc1..6.txt (the mccad call sites).
 *
 * get_plane_info(short prompt_id, PLANE_sf &plane, char *ok, CWnd *parent, HINSTANCE res, uint cmd, PlaneEventData *ped) runs its own
 * get_event loop with the modeless dialog up; the caller's loop is blocked meanwhile. Here it is a state machine: psel_begin opens it,
 * the front end feeds the get_event events (psel_click = graphics events 1 / 2, psel_ctl = the dialog's controls -> the 0x8028
 * messages 100..110 = get_event event 0x200, psel_esc = get_event returning 1), and the caller's callback runs where get_plane_info
 * returns. PLANE_sf (world): +0 origin, +0x18 normal, +0x30 the plane's X direction - PlaneSf here.
 *
 * Refused with a message (formats/plane_selection.md section 7): a surface picked by a click (its (u, v) comes from the display-list
 * points, FUN_18013cbc0 - formats/chaining.md "Which routine normal picking uses", NOT PORTED there either), solid edges (event 0x4000),
 * cross hatching (XH_ID 0x1000: none here), selection cues other than X / Y / Z typed into an edit, expressions in the edits (the
 * evaluator UIEdit::Evaluate calls, m_pDfltEval, is not decoded), the '?' menu of an edit, a solid made in this session (faces of part
 * solids only); not ported: the PlaneEventData copy and cmd != 0 (no caller passes them), Help. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core_ui.h"
#include "../mcsurf.h"
#include "../mcsurf_nrm.h"
#include "../mcsrfcrv.h"
#include "../mcam_saveops.h"
#include "../occt_prim.h"

void mcsb_curv_geom(double o[6][3], double *k1, double *k2);   /* surf_curv_geom 0x18047ced0 (mcsurf_build.c) */

/* ------------------------------------------------------------------------------------------------ the dialog template (1085) */
/* mccoreres.dll dialog 1085 (tools/pe_dialogs.py: decomp/planesel/mccoreres_dialogs.tsv), 143 x 268 du; the bitmap buttons'
   images from CPlaneDlg FUN_180238ff0 (SetBitmap, 0xc0c0c0 transparent): mccoreres unless noted; text = the "|tooltip" */
const PselCtl PSEL1085[PSEL1085_N] = {
    { PSK_GROUP, 963, 7, 7, 128, 229, "Plane", 0, 0 },
    { PSK_BMP, 4466, 13, 25, 20, 18, "X", 5674, 0 },
    { PSK_EDIT, 964, 35, 27, 50, 14, "", 0, 0 },
    { PSK_BMP, 4480, 88, 25, 20, 18, "Select point", 5673, 0 },               /* DDX 0x1180 -> +0x6e78: SetBitmap 0x1629 */
    { PSK_BMP, 4483, 110, 25, 20, 18, "Construction depth", -3451, 0 },        /* DDX 0x1183 -> +0x9db8: 0xd7b from GetMastercamResourceHandle = opmanres (it holds 3451 and the cue strings) */
    { PSK_BMP, 4467, 13, 48, 20, 18, "Y", 5675, 0 },
    { PSK_EDIT, 965, 35, 50, 50, 14, "", 0, 0 },
    { PSK_BMP, 4481, 88, 48, 20, 18, "Select point", 5673, 0 },
    { PSK_BMP, 4485, 110, 48, 20, 18, "Construction depth", -3451, 0 },
    { PSK_BMP, 4468, 13, 71, 20, 18, "Z", 5676, 0 },
    { PSK_EDIT, 966, 35, 73, 50, 14, "", 0, 0 },
    { PSK_BMP, 4482, 88, 71, 20, 18, "Select point", 5673, 0 },
    { PSK_BMP, 4486, 110, 71, 20, 18, "Construction depth", -3451, 0 },
    { PSK_BMP, 1258, 53, 101, 36, 18, "Line", 1126, 0 },
    { PSK_BMP, 1262, 53, 122, 36, 18, "Three Autocursor Points", 1198, 0 },
    { PSK_BMP, 1265, 53, 143, 36, 18, "Entities", 1199, 0 },
    { PSK_BMP, 1269, 53, 164, 36, 18, "Normal", 1200, 0 },
    { PSK_BMP, 4470, 53, 185, 36, 18, "Named", 1197, 0 },
    { PSK_BMP, 575, 53, 206, 36, 18, "Flip", 1123, 0 },                         /* 1124 while the flip flag is set (FUN_180238960) */
    { PSK_BTN, 1, 24, 243, 36, 18, "OK", 0, 0 },
    { PSK_BTN, 2, 62, 243, 36, 18, "Cancel", 0, 0 },
    { PSK_BTN, 9, 100, 243, 36, 18, "Help", 0, -1 },                           /* LaunchHelp: NOT PORTED */
};

/* mccoreres strings (decomp/planesel/mccoreres.json) */
#define S7436 "Select Plane"
#define S7437 "Select line in construction plane"
#define S7438 "Select a flat entity, 2 lines, or 3 points"
#define S1872 "Select a point"                 /* "\nSelect a point": the cue's prompt (GetCoreString 0x750); the leading newline left out */
#define S1878 "Select first point"
#define S1879 "Select second point"
#define S5433 "Select third point"
#define S5094 "Select the normal line"
#define S5330 "Select a second line"
#define S5011 "Warning - zero length line"
#define S5346 "Selected lines do not lie in a plane"
#define S5345 "Cannot define a plane - lines are parallel"
#define S5347 "Selected points are either collinear or 2 of them are coincident"
#define S5329 "Selected entity is not flat"

/* ------------------------------------------------------------------------------------------------ state */
int psel_open;                                  /* get_plane_info runs */
static PlaneSf *P;                              /* param_2: the caller's plane, written in place */
static PlaneSf ps_save; static int ps_save_set; /* local_d648: the plane on entry (Cancel / Esc restore it); written only for a valid plane */
static int ps_ok;                               /* *param_3 */
static int ps_mode;                             /* iVar46: 0 none, 1 Line, 2 Three points, 3 Entities, 4 Normal */
static unsigned ps_ev = 0x200, ps_sel = 0x8e;   /* uVar45 get_event's event mask, local_d960 its entity mask */
static int ps_xk, ps_xk2;                       /* bVar18: the X direction is set (else made at the end); bVar19 (Named's Cancel) */
static int ps_npt, ps_nln, ps_third;            /* local_d940, local_d91c, local_d976 */
static double ps_pts[3][3], ps_ln1[6];          /* local_1d8 / local_1c0 / local_1a8; local_d828 (the first line) */
static char ps_prompt0[256], ps_prompt[256];    /* local_d948 (the caller's prompt), local_d970 */
static int ps_nowait;                           /* DAT_181444171 (get_plane_info_without_waiting) */
static PselDone ps_done; static void *ps_ctx;
/* the dialog (CPlaneDlg): the MCReal values +0x5090 / +0x5e88 / +0x6c80, the edits' texts, the last committed values +0x12b78..,
   the flip flag +0x12b90 */
double psel_val[3]; char psel_text[3][64]; static double ps_last[3]; int psel_flip;
int psel_cue;                                   /* SelectionCue running for X 1 / Y 2 / Z 3 (the dialog suspended) */
int psel_named;                                 /* SelectPlaneFromList (842) open over the dialog */
int psel_upd;                                   /* counts UpdateData(FALSE) - the front end re-reads the texts */
static int ps_drawn; static double ps_do[3], ps_dn[3];   /* draw_plane's GrTemp lines (DAT_1813fb915) */

/* ------------------------------------------------------------------------------------------------ helpers */
static void v_sub(const double a[3], const double b[3], double o[3]) { for (int k = 0; k < 3; k++) o[k] = a[k] - b[k]; }
static double v_len(const double a[3]) { return sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]); }
static void v_unit(double a[3]) {                        /* FUN_18000f2f0: 0 stays 0 */
    double l = v_len(a); if (l == 0.0) { a[0] = a[1] = a[2] = 0.0; } else { a[0] /= l; a[1] /= l; a[2] /= l; }
}
static void cross(const double a[3], const double b[3], double o[3]) {
    double x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0]; o[0] = x; o[1] = y; o[2] = z;
}
static double tol(void) { return mc_systol.tol; }
/* mastercam.dll view_to_world / world_to_view (formats/mcam.md "Arc views"): views < 2 identity, else M^T v / M v with the view's rows */
static int view_mat(int view, double m[9]) {
    if (view >= 1 && view <= 8 && mcam_std_view(view, m)) return 1;
    for (int k = 0; k < F.nfviews; k++) if (F.fviews[k].n == view) { memcpy(m, F.fviews[k].m, sizeof F.fviews[k].m); return 1; }
    return 0;
}
static void v2w_m(const double *m, const double v[3], double w[3]) { double t[3]; for (int c = 0; c < 3; c++) t[c] = m[c] * v[0] + m[3 + c] * v[1] + m[6 + c] * v[2]; memcpy(w, t, sizeof t); }
static void w2v_m(const double *m, const double w[3], double v[3]) { double t[3]; for (int r = 0; r < 3; r++) t[r] = m[3 * r] * w[0] + m[3 * r + 1] * w[1] + m[3 * r + 2] * w[2]; memcpy(v, t, sizeof t); }
static void view_to_world(int view, const double v[3], double w[3]) {
    double m[9]; if (view < 2 || !view_mat(view, m)) { memmove(w, v, 3 * sizeof *v); return; } v2w_m(m, v, w);
}
/* the active Cplane (MastercamPlanes::GetActiveViewNo(Planes, 0)): its matrix (cam_plane_matrix: rows x / y / z) and the active origin
   (GetActiveOrigin(0): SubtractActiveOrigin / AddActiveOrigin) */
static const double *cpl_m(void) { return cam_plane_matrix(plane_active[0] >= 1 ? plane_active[0] : 1); }
static const double *cpl_org(void) { return plane_active_origin(0); }
static void cpl_v2w(const double v[3], double w[3]) { v2w_m(cpl_m(), v, w); }
static void cpl_w2v(const double w[3], double v[3]) { w2v_m(cpl_m(), w, v); }
/* FUN_180a2d0e0: every coordinate below 1e38, |normal| > tol and |X direction| > tol */
static int plane_valid(const PlaneSf *p) {
    const double *d = &p->o[0];
    for (int k = 0; k < 9; k++) if (!(fabs(d[k]) < 1e38)) return 0;
    return v_len(p->n) > tol() && v_len(p->x) > tol();
}
static void post_warning(const char *s) { cui_message("Mastercam", "%s", s); }   /* post_warning(id, mccoreres, the dialog) */
static void set_prompt(const char *s) { snprintf(ps_prompt, sizeof ps_prompt, "%s", s); snprintf(cam_msg, sizeof cam_msg, "%s", ps_prompt); overlay_dirty = 1; }
static void prompt_reset(void) { set_prompt(ps_prompt0); }   /* Ordinal_1499(local_d970, local_d948) */
/* draw_plane (mccore 0x180539ae0) / erase_plane (0x18053ae10): the GrTemp lines, drawn by psel_overlay */
static void draw_plane(void) { memcpy(ps_do, P->o, sizeof ps_do); memcpy(ps_dn, P->n, sizeof ps_dn); ps_drawn = 1; overlay_dirty = 1; }
static void erase_plane(void) { ps_drawn = 0; overlay_dirty = 1; }
/* make_mcreal_str (mccore 0x1804dd8d0): |v| >= 1e38 -> "$$$$$$$$$$$$$$$"; else "%.*f" (sign mode MCReal +0xd88 = 0: no '+'); MCReal
   +0xd9c (1) trailing zeros cut down to one after the point */
static void mcreal_str(double v, int places, char *b, size_t n) {
    if (1e+38 < fabs(v) || fabs(v) == 1e+38) { snprintf(b, n, "$$$$$$$$$$$$$$$"); return; }
    snprintf(b, n, "%.*f", places, v);
    size_t l = strlen(b);
    while (l > 1 && b[l - 1] == '0' && b[l - 2] != '.') b[--l] = 0;
}
/* the dialog's MCReals: MCReal::MCReal(void) 0x18050f570: +0xd94 = 6 places, +0xd96 0, +0xd88 0, +0xd9c 1, +0xd84 0 (not an angle), +0xd80 1
   (selection cues), +0xd90 0, bounds +0xbf0 / +0xbf8 = -1e38 / 1e38 (CPlaneDlg ctor FUN_180238280 sets +0xd98 = 1 and +0xd90 = 0) */
#define PS_PLACES 6
/* DDX_ProcessEditReal (mccore 0x1804dbd20), load (UpdateData(FALSE)): the text = make_mcreal_str(value, 6) */
static void update_data_false(void) { for (int i = 0; i < 3; i++) mcreal_str(psel_val[i], PS_PLACES, psel_text[i], sizeof psel_text[i]); psel_upd++; }
char psel_errtip[160]; int psel_errtip_axis = -1;   /* UIEditReal::DisplayErrorTip's text and its edit (the front end shows it) */
static int ps_cue_typed = -1;                        /* the edit whose typed cue letter runs the point pick (-1: a "Select point" button) */
static int ps_cue_cancel = -1;                       /* the edit whose typed cue was cancelled (its text then fails Validate) */
static int ps_resume = 0;                            /* the handler UpdateData(TRUE) runs in, run again after the typed cue's pick: control id, or -(964 + axis) */
/* the selection cue strings (mastercam.dll CMastercamApp::LoadSelectionCues 0x18028faa0: opmanres 26980..26994, "X  =  X coordinate of a
   point", "Y ..", "Z ..", "R ..", "D ..", "L ..", "S ..", "Angle menu", "O ..", "T ..", "P ..", "W ..", "H ..", "Q ..", "B ..");
   FUN_1804dccb0: the cue = 1 + the index of the first whose first letter is the typed one */
static const char PS_CUES[] = "XYZRDLSAOTPWHQB";
/* IsSelectionCueString (0x1804dd6c0): fewer than 0x15 characters, spaces trimmed, one character, in "XYZRDLSHQBOTPW" (upper-cased) */
static int cue_letter(const char *t) {
    char b[64]; int n = 0; for (const char *q = t; *q && n < 63; q++) if (*q != ' ' && *q != '\t') b[n++] = *q; b[n] = 0;
    if (strlen(t) >= 0x15 || n != 1) return 0;
    char c = b[0] >= 'a' && b[0] <= 'z' ? (char)(b[0] - 32) : b[0];
    return strchr("XYZRDLSHQBOTPW", c) && c ? c : 0;
}
/* UIEditReal::InvalidRealField (uictrls 0x180013c10): the error tip "<min> to <max>" (each "%.*f" at n_places_an = config 29003; -1e38 /
   1e38 here) and the text back to the value (the same format); the DDX then fails (CDataExchange::Fail: the fields after it are not read) */
static void invalid_real_field(int i) {
    int pl = cfg_places >= 0 ? cfg_places : 4;
    snprintf(psel_errtip, sizeof psel_errtip, "%.*f to %.*f", pl, -1e+38, pl, 1e+38); psel_errtip_axis = i;
    snprintf(psel_text[i], sizeof psel_text[i], "%.*f", pl, psel_val[i]); psel_upd++;
}
/* DDX_ProcessEditReal, save (UpdateData(TRUE)), each edit in DoDataExchange order (964, 965, 966): the text; '?' posts the edit's
   right-click (its menu: NOT PORTED - refused); a selection cue (MCReal +0xd80 = 1) runs SelectionCue first; UIEditReal::Validate
   (0x180013de0): non-empty, UIEdit::Evaluate (here: a plain number only - the evaluator object m_pDfltEval NOT DECODED, other text refused),
   "%.12f" of it matching "\\d.\\d", within the bounds; else InvalidRealField and the exchange fails; the text = make_mcreal_str of the
   value. Returns 1 ok, 0 failed (as UpdateData FALSE: the callers here do not look), 2 a typed cue's point pick started (the handler is
   run again after it: ps_resume) */
static int update_data_true(int resume) {
    for (int i = 0; i < 3; i++) {
        const char *t = psel_text[i];
        if (t[0] == '?') { snprintf(cam_msg, sizeof cam_msg, "Plane Selection: the edit's '?' menu (WM_RBUTTONDOWN) is NOT PORTED"); continue; }
        char c = (char)cue_letter(t);
        if (c && ps_cue_cancel != i) {
            int cue = (int)(strchr(PS_CUES, c) - PS_CUES) + 1;
            if (cue <= 3) { psel_cue = cue; ps_cue_typed = i; ps_resume = resume; snprintf(ps_prompt, sizeof ps_prompt, "%s", "Select a point"); snprintf(cam_msg, sizeof cam_msg, "%s", ps_prompt); overlay_dirty = 1; return 2; }
            snprintf(cam_msg, sizeof cam_msg, "Plane Selection: the selection cue '%c' (FUN_180510950 case %d) is NOT PORTED", c, cue);
        }
        char *end = NULL; double v = 0; int ok = 0;
        if (t[0]) { v = strtod(t, &end); while (end && (*end == ' ' || *end == '\t')) end++; ok = end && end != t && !*end; }
        if (!ok && t[0] && !c) snprintf(cam_msg, sizeof cam_msg, "Plane Selection: an expression in the edit (UIEdit::Evaluate's evaluator) is NOT DECODED");
        if (!ok || !(-1e+38 <= v && v <= 1e+38)) { invalid_real_field(i); ps_cue_cancel = -1; return 0; }
        psel_val[i] = v; mcreal_str(v, PS_PLACES, psel_text[i], sizeof psel_text[i]);
    }
    ps_cue_cancel = -1; psel_upd++;
    return 1;
}

/* ------------------------------------------------------------------------------------------------ the loop's end */
static void ps_finish(void) {
    if (ps_ok) {
        if (!ps_xk) {                                 /* no X direction yet: (0,1,0) x n, or (1,0,0) x n when n is within 0.1 of +-Y */
            const double *n = P->n; double r[3] = { 0, 1, 0 };
            if (!(0.1 < fabs(fabs(n[1]) - 1.0) || fabs(fabs(n[1]) - 1.0) == 0.1)) { r[0] = 1; r[1] = 0; }
            cross(r, n, P->x);
        }
        v_unit(P->x);
    }
    cam_msg[0] = 0;                                   /* cleartext, clearprompt */
    psel_open = 0; psel_cue = 0; psel_named = 0; ps_nowait = 0; erase_plane();   /* (the dialog's DestroyWindow; the window rect is kept) */
    PselDone cb = ps_done; void *ctx = ps_ctx; ps_done = NULL; ps_ctx = NULL;
    if (cb) cb(ps_ok, P, ctx);
}
/* the bottom of the loop: get_plane_info_without_waiting leaves at the first plane */
static void ps_tail(int *go_on) {
    if (ps_nowait && ps_ok) *go_on = 0;
    if (!*go_on) ps_finish();
}

/* ------------------------------------------------------------------------------------------------ psel_begin */
void psel_begin(const char *prompt, PlaneSf *plane, PselDone done, void *ctx, int nowait) {
    if (psel_open) return;
    P = plane; ps_done = done; ps_ctx = ctx; ps_nowait = nowait;
    psel_open = 1; ps_ok = 0; ps_mode = 0; ps_ev = 0x200; ps_sel = 0x8e; ps_npt = ps_nln = ps_third = 0; psel_cue = 0; psel_named = 0;
    ps_xk = ps_xk2 = 1;                                /* bVar18 / bVar19 true before the loop */
    memset(ps_pts, 0, sizeof ps_pts); memset(ps_ln1, 0, sizeof ps_ln1);
    /* CPlaneDlg ctor FUN_180238280: values, last values and the flip flag 0; OnInitDialog FUN_180238a20 ends with UpdateData(FALSE) */
    memset(psel_val, 0, sizeof psel_val); memset(ps_last, 0, sizeof ps_last); psel_flip = 0; update_data_false();
    snprintf(ps_prompt0, sizeof ps_prompt0, "%s", prompt && *prompt ? prompt : S7436);   /* param_1 == 0: mccoreres 7436 */
    ps_save_set = 0;                                   /* an invalid plane: local_d648 ([RBP+0x270]) is never written - Cancel / Esc would copy an
                                                          uninitialised stack copy (every access: decomp/planesel gpi.asm 0x180a2b27c / b3f3 / b617);
                                                          that restore is refused here: the plane stays as it is, ok 0 */
    psel_errtip[0] = 0; psel_errtip_axis = -1; ps_cue_typed = ps_cue_cancel = -1; ps_resume = 0;
    if (!plane_valid(P)) {                             /* origin 0, normal (0,0,1), X (1,0,0) */
        memset(P, 0, sizeof *P); P->n[2] = 1.0; P->x[0] = 1.0;
    } else {
        /* the origin in the Cplane (SubtractActiveOrigin, world_to_view): a single non-zero coordinate goes to its field, otherwise all
           0 (FUN_180239400 sets the values only: the texts keep OnInitDialog's) */
        double v[3], d[3]; v_sub(P->o, cpl_org(), d); cpl_w2v(d, v);
        double val[3] = { 0, 0, 0 };
        if (v[0] == 0.0) {
            if (!isnan(v[1]) && v[1] == 0.0 && v[2] != 0.0) val[2] = v[2];
            else if (!(v[1] == 0.0) && !(v[2] != 0.0)) val[1] = v[1];
        } else if (v[1] == 0.0 && v[2] == 0.0) val[0] = v[0];
        cpl_v2w(v, d); for (int k = 0; k < 3; k++) P->o[k] = d[k] + cpl_org()[k];   /* view_to_world, AddActiveOrigin */
        memcpy(psel_val, val, sizeof val);
        ps_save = *P; ps_save_set = 1;                 /* FUN_180a2a510(plane, local_d648) */
    }
    /* then the plane is put through view_to_world of the Cplane once more (origin, normal, X; no origin added): as Mastercam does */
    cpl_v2w(P->o, P->o); cpl_v2w(P->n, P->n); cpl_v2w(P->x, P->x);
    draw_plane();
    set_prompt(ps_prompt0);
    overlay_dirty = 1;
}

/* ------------------------------------------------------------------------------------------------ dialog messages (event 0x200) */
static void ps_xyz(int axis) {                         /* 0x66 / 0x67 / 0x68: FUN_180238760 reads the values; the plane through the value on the axis */
    erase_plane();
    double o[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 }; o[axis] = psel_val[axis]; n[axis] = 1.0;
    cpl_v2w(o, P->o); for (int k = 0; k < 3; k++) P->o[k] += cpl_org()[k];
    cpl_v2w(n, P->n);
    ps_xk = ps_xk2 = 0;
    draw_plane(); ps_ok = 1;
}
static void ps_named_done(int id);
static void ps_msg(int code, int *go_on) {
    switch (code) {
    case 100:                                          /* OK */
        erase_plane(); *go_on = 0;
        ps_ok = plane_valid(P);                        /* (the PlaneEventData copy: NOT PORTED) */
        break;
    case 0x65:                                         /* Cancel */
        erase_plane(); *go_on = 0;
        if (ps_save_set) { *P = ps_save; ps_ok = plane_valid(P); } else ps_ok = 0;
        break;
    case 0x66: ps_xyz(0); break;
    case 0x67: ps_xyz(1); break;
    case 0x68: ps_xyz(2); break;
    case 0x69: set_prompt(S7437); ps_sel = 2; ps_ev |= 0x4002; ps_mode = 1; break;
    case 0x6a: set_prompt(S1878); ps_sel = 1; ps_ev |= 1; ps_mode = 2; break;
    case 0x6b: set_prompt(S7438); ps_sel = 0x19ff; ps_ev = 0x6202; ps_mode = 3; break;
    case 0x6c: set_prompt(S5094); ps_sel = 2; ps_ev |= 0x4002; ps_mode = 4; break;
    case 0x6d: psel_named = 1; pls_begin(4); break;     /* SelectPlaneFromList(0, NULL, 0, 0): nothing preselected; ps_named_done follows */
    case 0x6e:                                          /* Flip */
        erase_plane(); for (int k = 0; k < 3; k++) P->n[k] *= -1.0;
        if (ps_xk) for (int k = 0; k < 3; k++) P->x[k] *= -1.0;
        draw_plane();
        break;
    }
}
/* the three points (LAB_180a2cbca): normal = unit((p2 - p1) x (p3 - p1)), origin = the centroid, X = unit(p2 - p1) */
static void ps_three(void) {
    ps_third = 0; prompt_reset(); ps_npt = 0;
    double a[3], b[3], c[3]; v_sub(ps_pts[1], ps_pts[0], a); v_sub(ps_pts[2], ps_pts[0], b); cross(a, b, c);
    double l = v_len(c);
    if (l <= tol()) { post_warning(S5347); return; }
    erase_plane();
    for (int k = 0; k < 3; k++) P->n[k] = l == 0.0 ? 0.0 : c[k] / l;
    for (int k = 0; k < 3; k++) P->o[k] = (ps_pts[0][k] + ps_pts[1][k]) * 0.3333333333333333 + ps_pts[2][k] * 0.3333333333333333;
    double al = v_len(a); for (int k = 0; k < 3; k++) P->x[k] = al == 0.0 ? 0.0 : a[k] / al;
    ps_xk = ps_xk2 = 1; draw_plane(); prompt_reset();
    ps_ev = 0x200; ps_mode = 0; ps_ok = 1;
}
static void ps_after_event(int *go_on) { if (ps_third) ps_three(); ps_tail(go_on); }

/* ------------------------------------------------------------------------------------------------ the front end's entries */
/* a dialog control (CPlaneDlg's message map 0x180d320c0; the one-line handlers decomp/planesel/small.asm) */
static void ps_post(int code) { int go_on = 1; ps_msg(code, &go_on); ps_after_event(&go_on); }
static void ps_commit_axis(int axis) {                 /* FUN_180239430 / 1802394f0 / 1802395b0: the other two 0, the text, UpdateData(FALSE), post */
    for (int i = 0; i < 3; i++) if (i != axis) psel_val[i] = 0.0;
    update_data_false();
    for (int i = 0; i < 3; i++) ps_last[i] = psel_val[i];
    ps_post(0x66 + axis);
}
int psel_ctl(int id) {
    if (!psel_open || psel_named || psel_cue) return 0;
    psel_errtip[0] = 0; psel_errtip_axis = -1;
    switch (id) {
    case 1: if (update_data_true(1) == 2) return 1; ps_post(100); return 1;   /* OnOK FUN_180238d10 (the OK button has the focus) -> UIDialog::OnOK
                                                                                 (uictrls 0x180009ef0) -> CDialog::OnOK (UpdateData(TRUE)); DestroyWindow, 100 */
    case 2: ps_post(0x65); return 1;                                 /* OnCancel FUN_1802388f0 */
    case 4466: case 4467: case 4468: if (update_data_true(id) == 2) return 1; ps_post(0x66 + (id - 4466)); return 1;   /* FUN_180238ec0 / ef0 / f20 */
    case 4480: case 4481: case 4482: psel_cue = id - 4480 + 1; ps_cue_typed = -1; set_prompt(S1872); return 1;   /* SelectionCue("X" / "Y" / "Z", the MCReal) */
    case 4483: case 4485: case 4486: { int ax = id == 4483 ? 0 : id == 4485 ? 1 : 2; psel_val[ax] = constr_depth; ps_commit_axis(ax); return 1; }   /* constr_depth */
    case 1258: ps_post(0x69); return 1;
    case 1262: ps_post(0x6a); return 1;
    case 1265: ps_post(0x6b); return 1;
    case 1269: ps_post(0x6c); return 1;
    case 4470: ps_post(0x6d); return 1;
    case 575: if (update_data_true(575) == 2) return 1; psel_flip = !psel_flip; ps_post(0x6e); return 1;   /* FUN_180238960 (bitmap 1124 when set, else 1123) */
    }
    return 0;
}
void psel_set_text(int axis, const char *t) { if (psel_open && axis >= 0 && axis < 3) snprintf(psel_text[axis], sizeof psel_text[axis], "%s", t); }
/* EN_KILLFOCUS of 964 / 965 / 966 and <Enter> in them (PreTranslateMessage FUN_180238f70): UpdateData(TRUE), a change above tol */
void psel_edit_done(int axis) {
    if (!psel_open || psel_named || psel_cue || axis < 0 || axis > 2) return;
    psel_errtip[0] = 0; psel_errtip_axis = -1;
    if (update_data_true(-(964 + axis)) == 2) return;
    if (tol() < fabs(ps_last[axis] - psel_val[axis])) ps_commit_axis(axis);
}
/* UDN_DELTAPOS of the edits' buddies (FUN_180238bf0 / c50 / cb0; EnableBuddyCtl(-100, 100) in OnInitDialog): value += delta x the spin step
   (no UpdateData first), then FUN_180239430 / ... The step: spin_inch_float / spin_metric_float (mastercam.dll; defaults FUN_1803bc480 0.1 /
   1.0, config 32001 / 32004 - FUN_1801d7810 writes them back) */
static double spin_step(void) {
    int metric = mcam_is_metric(&F) == 1; char b[64]; mcam_config_value(metric, metric ? 32004 : 32001, b, sizeof b);
    return b[0] ? atof(b) : metric ? 1.0 : 0.1;
}
void psel_spin(int axis, int delta) {
    if (!psel_open || psel_named || psel_cue || axis < 0 || axis > 2) return;
    psel_val[axis] = (double)delta * spin_step() + psel_val[axis];
    ps_commit_axis(axis);
}
static void ps_resume_handler(void) {
    int r = ps_resume; ps_resume = 0;
    if (r < 0) psel_edit_done(-r - 964); else if (r > 0) psel_ctl(r);
}
/* Esc: the 0x802a message -> RunPump returns 1, FUN_1804e34f0 returns 1 (no window / polygon being drawn): get_event returns 1 and its
   accept flag = (return == 0) = 0 (FUN_1804e9920, decomp/mccore_getevent.c; decomp/getevent/mccore_getevent_loop.c FUN_1804e8dd0 /
   FUN_1804e4d60; formats/backplot.md 0x802a): the plane on entry back (when it was saved), and the loop ends */
void psel_esc(void) {
    if (!psel_open) return;
    if (psel_named) return;                              /* the list's own Esc = its Cancel */
    if (psel_cue) {                                      /* FUN_18027b600: no point - SelectionCue false */
        psel_cue = 0; prompt_reset();
        if (ps_cue_typed >= 0) { ps_cue_cancel = ps_cue_typed; ps_cue_typed = -1; ps_resume_handler(); }   /* the typed letter then fails Validate */
        return;
    }
    erase_plane(); if (ps_save_set) { *P = ps_save; ps_ok = plane_valid(P); } else ps_ok = 0; ps_finish();
}
/* SelectPlaneFromList's answer (cui_planes.c pls_ok / pls_cancel with pls_which 4): id 0 = cancelled */
static void ps_named_done(int id) {
    psel_named = 0;
    int go_on = 1;
    if (id < 1) { ps_xk = ps_xk2; ps_post(0x65); return; }   /* GetPlanePtrByID NULL: PostMessage 0x65 */
    plane_set_active(0, id);                                  /* MastercamPlanes::SetActivePlane(Planes, 0 (Cplane), id) */
    erase_plane();
    const double *m = cam_plane_matrix(id);                   /* the plane's view (GetShortViewNo): row 3 = the normal, row 1 = X */
    P->n[0] = m[6]; P->n[1] = m[7]; P->n[2] = m[8]; v_unit(P->n);
    P->x[0] = m[0]; P->x[1] = m[1]; P->x[2] = m[2];
    memcpy(P->o, plane_origin(id), sizeof P->o);              /* Plane::GetOriginInWorldCoordinates */
    ps_xk2 = 1; ps_xk = 1; draw_plane(); ps_ok = 1;
    ps_after_event(&go_on);
}
void psel_named_answer(int id) { if (psel_open && psel_named) ps_named_done(id); }

/* a point event (get_event event 1, mask bit 0: AutoCursor) */
static void ps_point(const double w[3]) {
    int go_on = 1;
    memcpy(ps_pts[ps_npt < 2 ? ps_npt : 2], w, 3 * sizeof *w);
    if (ps_npt == 0) { set_prompt(S1879); ps_npt = 1; }
    else if (ps_npt == 1) { set_prompt(S5433); ps_npt = 2; }
    else { ps_three(); ps_tail(&go_on); return; }
    ps_after_event(&go_on);
}
/* the selection cue's point: MCReal::SelcueEnd -> FUN_180510950 cue 1..3 -> FUN_18060f350(PlaneType 0 = Cplane): world_to_view(point, the
   Cplane's view), less GetActiveOriginInViewCoordinates when MastercamPlanes::ActiveOriginIsOn(0) (mastercam.dll 0x1802c4830: the active
   origin's length above its P3D tolerance, 2e-7 from P3D::P3D; GetProdEnableBits() & 0x10 forces it on - the license bits, 0 here as in
   every port), coordinate x / y / z (CoordDisp 0). SelcueEnd: the value (bounds -1e38 .. 1e38) and the text make_mcreal_str(v, 6) */
static void ps_cue_point(const double w[3]) {
    int ax = psel_cue - 1; psel_cue = 0; prompt_reset();
    const double *o = cpl_org(); double v[3], ov[3]; cpl_w2v(w, v);
    if (2e-07 < v_len(o)) { cpl_w2v(o, ov); for (int k = 0; k < 3; k++) v[k] -= ov[k]; }
    if (ps_cue_typed >= 0) {                                    /* a typed cue: the edit's text, then the handler again (its UpdateData reads it) */
        int i = ps_cue_typed; ps_cue_typed = -1;
        mcreal_str(v[ax], PS_PLACES, psel_text[i], sizeof psel_text[i]); psel_upd++;
        ps_resume_handler(); return;
    }
    psel_val[ax] = v[ax];
    ps_commit_axis(ax);                                         /* SelectionCue true: FUN_180239430 / .. */
}

/* SplineIsFlat (mccore 0x180a2a970): the first normal from the start and two samples, then every sample's distance along it */
static int spline_flat(const CurveRec *c, double n[3]) {
    int cnt = c->cls == 8 ? (int)*(const int32_t *)(c->rec + 0xb0) : (int)*(const int32_t *)(c->rec + 0xcc);
    double t1, t2; mcs_spline_t1t2(c, &t1, &t2);
    double dt = (t2 - t1) / (double)(cnt + 9), p0[3], p1[3], p[3], t = t1;
    if (mcs_curve_eval(&F, c, t, p0)) return -1;
    t += dt; if (mcs_curve_eval(&F, c, t, p1)) return -1;
    t += dt; if (mcs_curve_eval(&F, c, t, p)) return -1;
    double a[3], b[3], q[3]; v_sub(p1, p0, a); v_sub(p, p0, b); cross(a, b, q);
    int none = 1; double l = v_len(q);
    if (tol() < l) { for (int k = 0; k < 3; k++) n[k] = q[k] / l; none = 0; }
    double eps = mcam_is_metric(&F) == 1 ? 0.0001 : 1e-06;
    for (int i = 3; i < cnt + 10; i++) {
        t += dt; if (mcs_curve_eval(&F, c, t, p)) return -1;
        v_sub(p, p0, b);
        if (none) { cross(a, b, q); l = v_len(q); if (tol() < l) { for (int k = 0; k < 3; k++) n[k] = q[k] / l; none = 0; } }
        else if (eps < b[0] * n[0] + b[1] * n[1] + b[2] * n[2]) return 0;   /* one-sided, as the DLL */
    }
    return !none;
}

/* dist_pt_l_3d (mccore 0x180013bd0): the distance of P from the line through l (the start when the line is shorter than tol) */
static double dist_pt_l_3d(const double p[3], const double l[6]) {
    double ax = l[0], ay = l[1], az = l[2], dx = l[3] - ax, dy = l[4] - ay, dz = l[5] - az;
    double cx = (p[1] - ay) * dz - (p[2] - az) * dy, cy = (p[2] - az) * dx - dz * (p[0] - ax), cz = (p[0] - ax) * dy - (p[1] - ay) * dx;
    double L = sqrt(dx * dx + dy * dy + dz * dz);
    if (fabs(L) <= tol()) return sqrt((ay - p[1]) * (ay - p[1]) + (ax - p[0]) * (ax - p[0]) + (az - p[2]) * (az - p[2]));
    return fabs(sqrt(cy * cy + cx * cx + cz * cz) / L);
}
/* FUN_180a2ad80 -> Plane::CheckFlat2lines(false, line 1, line 2) (mastercam.dll 0x1803d7f70; decomp/planesel/flat2.c): both lines at
   least tol long (Distance_P3D_P3D) and Plane::GetMatrixFrom2lines (0x1803da0d0): rows = the unit directions (each reversed when its
   end is nearer the other line than its start), row 3 = row 1 x row 2 above 2e-7, unit, row 2 = row 3 x row 1 of length 1 within 2e-7;
   failing that once more with line 2's start replaced by line 1's. With the first argument false the four points' distances along
   row 3 are not compared: two skew lines pass. */
static int flat2lines(const double l1[6], const double l2[6]) {
    double t[3]; v_sub(l1 + 3, l1, t); if (v_len(t) < tol()) return 0;
    v_sub(l2 + 3, l2, t); if (v_len(t) < tol()) return 0;
    double L[2][6]; memcpy(L[0], l1, sizeof L[0]); memcpy(L[1], l2, sizeof L[1]);
    for (int it = 0; it < 2; it++) {
        if (it == 1) memcpy(L[1], L[0], 3 * sizeof(double));
        double R[3][3];
        for (int i = 0; i < 2; i++) {
            v_sub(L[i] + 3, L[i], R[i]); v_unit(R[i]);
            double a = dist_pt_l_3d(L[i], L[1 - i]), b = dist_pt_l_3d(L[i] + 3, L[1 - i]);
            if (b < a) for (int k = 0; k < 3; k++) R[i][k] *= -1.0;
        }
        cross(R[0], R[1], R[2]);
        if (2e-07 < v_len(R[2])) { v_unit(R[2]); cross(R[2], R[0], R[1]); double l = v_len(R[1]); if (fabs(l - 1.0) <= 2e-07 && fabs(l - 1.0) != 2e-07) return 1; }
    }
    return 0;
}
/* get_current_sel_mask() +0xc (mccore 0x180657590): the Selection bar's hard / maintained / one-time masks; none set here (that bar's masks
   are not part of this program) = 0x1f1fff (formats/wireframe.md, cui_sel.c spl_skip: the idle pick's rule) */
#define SEL_NONE_SET 0x1f1fff
/* an entity's class (ent +0) */
static unsigned ent_class(int e) {
    const Ent *en = &F.ents[e];
    if (en->type == E_SPLINE) return en->cls;
    if (en->type == E_NOTE && en->nt >= 0 && en->nt < F.nnotes) return *(const uint32_t *)F.notes[en->nt].E;
    return (unsigned)en->type;
}
/* a drafting record (N_ID 0x10 / W_ID 0x20 at +0x70 / view +0x88; D_ID 0x40 at +0x310 / view +0x328) */
static void ps_drafting(const unsigned char *E) {
    unsigned c = *(const uint32_t *)E; double o[3]; int view;
    if (c == 0x40) { memcpy(o, E + 0x310, sizeof o); view = *(const int16_t *)(E + 0x328); }
    else { memcpy(o, E + 0x70, sizeof o); view = *(const int16_t *)(E + 0x88); }
    double z[3] = { 0, 0, 1 }, x[3] = { 1, 0, 0 };
    memcpy(P->o, o, sizeof o); view_to_world(view, z, P->n); view_to_world(view, x, P->x);
    ps_xk = ps_xk2 = 1;
}
/* an entity event (get_event event 2) in Line 1 / Entities 3 / Normal 4; T = the pick's parameter (lines: 0..1 along p1 -> p2;
   surfaces: (T[0], T[1]) = (u, v)); DE = a drafting record (pick through cui_draft_pick) instead of an entity */
static void ps_entity(int e, const unsigned char *DE, const double T[2]) {
    int go_on = 1;
    if (ps_mode == 1) {                                   /* Line: the plane through the line and the Cplane's Z */
        if (e < 0 || F.ents[e].type != E_LINE) { ps_tail(&go_on); return; }
        const Ent *en = &F.ents[e]; double p1[3] = { en->a.x, en->a.y, en->a.z }, p2[3] = { en->b.x, en->b.y, en->b.z }, d[3], v[3];
        v_sub(p1, p2, d); cpl_w2v(d, v);
        if (tol() <= fabs(v[0]) || tol() <= fabs(v[1])) {
            erase_plane(); const double *Z = cpl_m() + 6;
            cross(Z, d, P->n); v_unit(P->n);
            memcpy(P->o, p1, sizeof p1);
            draw_plane();
            v_sub(p2, p1, P->x);
            ps_xk2 = 1; prompt_reset(); ps_ev = 0x200; ps_mode = 0; ps_ok = 1; ps_xk = 1;
        } else post_warning(S5011);
        ps_after_event(&go_on); return;
    }
    if (ps_mode == 4) {                                   /* Normal: from the end nearer the pick towards the other */
        if (e < 0 || F.ents[e].type != E_LINE) { ps_tail(&go_on); return; }
        const Ent *en = &F.ents[e]; double p1[3] = { en->a.x, en->a.y, en->a.z }, p2[3] = { en->b.x, en->b.y, en->b.z };
        erase_plane();
        if (0.5 < T[0] || T[0] == 0.5) { memcpy(P->o, p2, sizeof p2); v_sub(p1, p2, P->n); }
        else { memcpy(P->o, p1, sizeof p1); v_sub(p2, p1, P->n); }
        v_unit(P->n);
        draw_plane(); ps_xk2 = 0; prompt_reset(); ps_ev = 0x200; ps_mode = 0; ps_ok = 1; ps_xk = 0;
        ps_after_event(&go_on); return;
    }
    if (ps_mode != 3) { ps_tail(&go_on); return; }
    /* Entities */
    if (DE) { ps_drafting(DE); goto taken; }
    if (e < 0) { ps_tail(&go_on); return; }
    const Ent *en = &F.ents[e]; unsigned c = ent_class(e);
    if (c == 1) {                                         /* a point: one of the three points */
        double w[3] = { en->a.x, en->a.y, en->a.z }; int k = ps_npt < 2 ? ps_npt : 2;
        memcpy(ps_pts[k], w, sizeof w);
        ps_sel = 1;
        if (ps_npt == 0) set_prompt(S1879); else if (ps_npt == 1) set_prompt(S5433); else ps_third = 1;
        ps_npt++;
        if (ps_ok) draw_plane();
        ps_after_event(&go_on); return;
    }
    if (c == 2) {                                         /* lines: the first, then the plane of the two */
        double l[6] = { en->a.x, en->a.y, en->a.z, en->b.x, en->b.y, en->b.z };
        if (ps_nln == 0) {
            set_prompt(S5330); memcpy(ps_ln1, l, sizeof l); ps_nln = 1; ps_sel = 2;
            if (ps_ok) draw_plane();
            ps_after_event(&go_on); return;
        }
        prompt_reset(); ps_nln = 0;
        if (!flat2lines(ps_ln1, l)) {                     /* FUN_180a2ad80 false */
            post_warning(S5346); ps_ok = 0; set_prompt(S7438); ps_sel = SEL_NONE_SET;
            ps_tail(&go_on); return;
        }
        double d1[3], d2[3], n[3]; v_sub(ps_ln1 + 3, ps_ln1, d1); v_sub(l + 3, l, d2); cross(d1, d2, n);
        if (v_len(n) < tol()) { double w[3]; v_sub(l, ps_ln1, w); cross(d1, w, n); }   /* parallel: line 1 and the join of the starts */
        double nl = v_len(n);
        if (nl < tol()) {
            post_warning(S5345); ps_ok = 0; set_prompt(S7438); ps_sel = SEL_NONE_SET;
            ps_tail(&go_on); return;
        }
        for (int k = 0; k < 3; k++) P->n[k] = nl == 0.0 ? 0.0 : n[k] / nl;
        memcpy(P->o, ps_ln1, 3 * sizeof *P->o);
        v_sub(ps_ln1 + 3, ps_ln1, P->x);
        ps_xk = ps_xk2 = 1; prompt_reset(); ps_ok = 1;
        ps_ev = 0x200; ps_mode = 0;
        draw_plane();
        ps_after_event(&go_on); return;
    }
    if (c == 4) {                                         /* an arc: its view through its centre */
        double z[3] = { 0, 0, 1 }, x[3] = { 1, 0, 0 };
        memcpy(P->o, &en->c, sizeof P->o);                 /* view_to_world(the view centre +0x98, view +0xc8) = the loader's world centre */
        if (en->vw >= 2) { v2w_m(en->vm, z, P->n); v2w_m(en->vm, x, P->x); }
        else { int v = en->avw; view_to_world(v, z, P->n); view_to_world(v, x, P->x); }
        ps_xk = ps_xk2 = 1; goto taken;
    }
    if (c == 8 || c == 0x80) {                            /* a spline: SplineIsFlat; origin = ent +0x68 (the stored start) */
        double n[3]; int r = en->crv >= 0 && en->crv < F.ncrvs ? spline_flat(&F.crvs[en->crv], n) : -1;
        if (r != 1) { post_warning(S5329); ps_tail(&go_on); return; }
        memcpy(P->n, n, sizeof n); P->o[0] = en->a.x; P->o[1] = en->a.y; P->o[2] = en->a.z;
        ps_xk = ps_xk2 = 0; goto taken;
    }
    if (c == 0x10 || c == 0x20) { ps_drafting(F.notes[en->nt].E); goto taken; }
    if (c == 0x100) {                                     /* a surface: flat at the pick (the larger principal curvature within tol) */
        const Surf *s = &F.surfs[en->srf]; double D[6][3];
        int r = mcsn_eval(&F, s, 2, T[0], T[1], D);
        if (r) { snprintf(cam_msg, sizeof cam_msg, "Plane Selection: eval_surf order 2 of this surface kind is NOT PORTED"); ps_tail(&go_on); return; }
        double k1, k2; mcsb_curv_geom(D, &k1, &k2);
        double k = fabs(k1) < fabs(k2) || fabs(k1) == fabs(k2) ? k2 : k1;
        double lim = mcam_is_metric(&F) == 1 ? tol() : tol() * 25.4;
        if (lim <= fabs(k) && fabs(k) != lim) { post_warning(S5329); ps_tail(&go_on); return; }
        double N[3]; if (mcsn_unit_norm(&F, s, D[1], D[2], T[0], T[1], N) < 0) { snprintf(cam_msg, sizeof cam_msg, "Plane Selection: the normal of this surface kind is NOT PORTED"); ps_tail(&go_on); return; }
        if (s->rec[0x6e] & 4) for (int i = 0; i < 3; i++) N[i] *= -1.0;
        memcpy(P->n, N, sizeof N); memcpy(P->o, D[0], sizeof P->o);
        ps_xk = ps_xk2 = 0; goto taken;
    }
    ps_tail(&go_on); return;                             /* 0x800 / 0x1000 (cross hatch: NOT PORTED) and the rest: nothing */
taken:
    prompt_reset(); ps_ok = 1; ps_ev = 0x200; ps_mode = 0;
    draw_plane();
    ps_after_event(&go_on);
}
/* the entity mask (get_event's 4th argument): the classes the pick may take */
static int ps_takes(unsigned c) { return (c & ps_sel) != 0; }
/* a solid face (get_event event 0x2000, any mode): sld_is_surf_planar(face) false or not planar: post_warning 5329 (and MessageBeep: not
   played here); else sld_check_face_normal: the normal and the point; drawn; X unknown; ok 1, prompt back, mask 0x200, mode 0
   (LAB_180a2b4bb) - occt_mesh.cpp occt_xt_face_plane */
static void ps_solid_face(int si, int fi) {
    int go_on = 1, planar = 0; double Pt[3], N[3]; char err[200] = "";
    double scale = mcam_is_metric(&F) == 1 ? 1000.0 : 39.37007874015748;
    if (occt_xt_face_plane(F.solids[si].xt, F.solids[si].xt_size, scale, fi, &planar, Pt, N, err, sizeof err)) { snprintf(cam_msg, sizeof cam_msg, "Plane Selection: solid face: %s", err); ps_tail(&go_on); return; }
    if (!planar) { post_warning(S5329); ps_tail(&go_on); return; }
    erase_plane();
    memcpy(P->n, N, sizeof N); memcpy(P->o, Pt, sizeof Pt);
    draw_plane(); ps_xk2 = 0; prompt_reset(); ps_ev = 0x200; ps_mode = 0; ps_ok = 1; ps_xk = 0;
    ps_after_event(&go_on);
}
/* a graphics click while the dialog runs (get_event's click, mccore FUN_1804e3b90 - decomp/getevent/mccore_getevent_msgs.c l.611-797): MX, MY
   the mouse, W the world point under it, WS the AutoCursor point and SNAPPED whether it snapped (FUN_1804e27d0). Order: a snap with the
   point bit (1) -> the point; else with the solid bits (0x7000): a solid face (0x2000) / edge (0x4000) under the cursor; else with the
   entity bits: select_ent_univ (an entity under the cursor); else with the point bit: the free point (map_cursor_to_3d: the construction
   plane at the construction depth). Every click belongs to get_event while the dialog is up: 1 */
int psel_click(float mx, float my, const double w[3], const double ws[3], int snapped) {
    if (!psel_open) return 0;
    if (psel_named) return 1;
    if (psel_cue) {                                      /* FUN_18027b600: get_event(prompt, ev, 1, 0, 0): a point (snapped or free) */
        double f[3] = { w[0], w[1], constr_depth }; ps_cue_point(snapped ? ws : f); overlay_dirty = 1; return 1;
    }
    if ((ps_ev & 1) && snapped) { ps_point(ws); overlay_dirty = 1; return 1; }
    if (ps_ev & 0x7000) {
        int si, fi; double hp[3];
        if (ps_ev & 0x2000) {
            int k = sld_pick_part_face(mx, my, &si, &fi, hp);
            if (k == 2) { snprintf(cam_msg, sizeof cam_msg, "Plane Selection: a solid made in this session: its faces are NOT PORTED (only the part's solids)"); overlay_dirty = 1; return 1; }
            if (k == 1) { ps_solid_face(si, fi); overlay_dirty = 1; return 1; }
        }
        int ei;
        if ((ps_ev & 0x4000) && ce_pick_edge(mx, my, &si, &ei)) { snprintf(cam_msg, sizeof cam_msg, "Plane Selection: a solid edge (event 0x4000, ps_edge_to_mc_curve) is NOT PORTED"); overlay_dirty = 1; return 1; }
    }
    if (ps_ev & 2) {
        int e = pick_entity(mx, my);
        if (e >= 0 && e < F.nents && ps_takes(ent_class(e))) {
            double T[2] = { 0, 0 };
            const Ent *en = &F.ents[e];
            if (en->type == E_LINE) {                      /* select_ent_univ: t_on_line (mccore 0x1804b48a0) of the cursor on the line, both through
                                                              proj_iso_pt (the graphics view's x / y axes); unclamped; 0 when shorter than tol */
                double c[2] = { vmat[0] * w[0] + vmat[1] * w[1] + vmat[2] * w[2], vmat[3] * w[0] + vmat[4] * w[1] + vmat[5] * w[2] };
                double a[2] = { vmat[0] * en->a.x + vmat[1] * en->a.y + vmat[2] * en->a.z, vmat[3] * en->a.x + vmat[4] * en->a.y + vmat[5] * en->a.z };
                double b[2] = { vmat[0] * en->b.x + vmat[1] * en->b.y + vmat[2] * en->b.z, vmat[3] * en->b.x + vmat[4] * en->b.y + vmat[5] * en->b.z };
                double dx = b[0] - a[0], dy = b[1] - a[1], d2 = dy * dy + dx * dx;
                T[0] = tol() * tol() < d2 ? (dy * (c[1] - a[1]) + dx * (c[0] - a[0])) / d2 : 0.0;
            } else if (en->type == E_SURF) {
                snprintf(cam_msg, sizeof cam_msg, "Plane Selection: a surface pick's (u, v) comes from the display-list points (FUN_18013cbc0) - NOT PORTED");
                overlay_dirty = 1; return 1;
            }
            ps_entity(e, NULL, T); overlay_dirty = 1; return 1;
        }
        if (ps_sel & 0x70) {                               /* notes / witness lines / dimensions: select_ent_univ's drafting cases (cui_draft_pick) */
            CuiDraftHit h;
            if (cui_draft_pick(w, (int)(ps_sel & 0x70), &h) && h.E) { double T[2] = { 0, 0 }; ps_entity(-1, h.E, T); overlay_dirty = 1; return 1; }
        }
    }
    if (ps_ev & 1) { double f[3] = { w[0], w[1], constr_depth }; ps_point(f); overlay_dirty = 1; return 1; }
    return 1;
}
const char *psel_prompt(void) { return ps_prompt; }

/* ------------------------------------------------------------------------------------------------ draw_plane's lines */
/* L = 60 / get_y_scr_scl (mastercam.dll 0x18033ab00: view +0x770 = pixels per model unit, decomp/mastercam_viewrot.c:334) = 60 pixels
   (here: vp_h / scene_view_height pixels per unit). a = unit(n x r), r = (0,1,0) when n is within 0.1 of
   +-X else (1,0,0); b = unit(a x n). Lines: -L b .. L b, -L a .. L a, the diamond of 0.75 L; draw_plane_arrow: o .. o + L n and the
   head (0.15 L across, 0.25 L long). Colour 8 when the background colour is 7 or 15, else 15; width 2 (the overlay's own width) */
void psel_overlay(float **pv, int *pn, int *pcap) {
    if (!psel_open || !ps_drawn) return;
    float rgb[3] = { 1, 1, 1 }; mc_rgb(((unsigned)(cfg_back_color - 7) & 0xf7u) == 0 ? 8 : 15, rgb);
    double L = 60.0 * scene_view_height() / (vp_h > 0 ? vp_h : 1);
    const double *o = ps_do, *n = ps_dn; double r[3] = { 1, 0, 0 };
    if (fabs(fabs(n[0]) - 1.0) < 0.1 || fabs(fabs(n[0]) - 1.0) == 0.1) { r[0] = 0; r[1] = 1; }
    double a[3], b[3]; cross(n, r, a); v_unit(a); cross(a, n, b); v_unit(b);
#define PT(s1, v1, s2, v2) do { V3 p = { o[0] + (s1) * (v1)[0] + (s2) * (v2)[0], o[1] + (s1) * (v1)[1] + (s2) * (v2)[1], o[2] + (s1) * (v1)[2] + (s2) * (v2)[2] }; ov_pt(pv, pn, pcap, p, rgb[0], rgb[1], rgb[2]); } while (0)
    double q = 0.75 * L;
    PT(-L, b, 0, a); PT(L, b, 0, a);
    PT(-L, a, 0, b); PT(L, a, 0, b);
    PT(-q, b, 0, a); PT(q, a, 0, b);
    PT(-q, b, 0, a); PT(-q, a, 0, b);
    PT(q, b, 0, a); PT(-q, a, 0, b);
    PT(q, b, 0, a); PT(q, a, 0, b);
    PT(0, n, 0, a); PT(L, n, 0, a);                       /* the arrow */
    PT(L, n, -0.15 * L, a); PT(L, n, 0.15 * L, a);
    PT(L, n, 0.15 * L, a); PT(1.25 * L, n, 0, a);
    PT(L, n, -0.15 * L, a); PT(1.25 * L, n, 0, a);
#undef PT
}

/* ------------------------------------------------------------------------------------------------ dump / tests */
void psel_dump(void) {
    printf("    planesel open %d mode %d ev 0x%x sel 0x%x npt %d nln %d ok %d xk %d cue %d named %d prompt \"%s\"\n",
           psel_open, ps_mode, ps_ev, ps_sel, ps_npt, ps_nln, ps_ok, ps_xk, psel_cue, psel_named, psel_open ? ps_prompt : "");
    printf("    planesel values %.6f %.6f %.6f texts \"%s\" \"%s\" \"%s\" last %.6f %.6f %.6f flip %d errtip %d \"%s\"\n", psel_val[0], psel_val[1], psel_val[2],
           psel_text[0], psel_text[1], psel_text[2], ps_last[0], ps_last[1], ps_last[2], psel_flip, psel_errtip_axis, psel_errtip);
    if (P) printf("    planesel plane o(%.6f,%.6f,%.6f) n(%.6f,%.6f,%.6f) x(%.6f,%.6f,%.6f) drawn %d\n", P->o[0], P->o[1], P->o[2], P->n[0], P->n[1], P->n[2],
                  P->x[0], P->x[1], P->x[2], psel_open && ps_drawn);
}
/* the tests' caller: a plane of its own, the answer printed (as a caller's code after get_plane_info) */
static PlaneSf ps_test_plane;
static void ps_test_done(int ok, PlaneSf *p, void *ctx) {
    (void)ctx;
    printf("    planesel result ok %d o(%.6f,%.6f,%.6f) n(%.6f,%.6f,%.6f) x(%.6f,%.6f,%.6f)\n", ok, p->o[0], p->o[1], p->o[2], p->n[0], p->n[1], p->n[2],
           p->x[0], p->x[1], p->x[2]);
}
/* steps: psel[:PROMPT] / pselp:9 numbers (the plane on entry) / pselnw (without waiting) - open it; pselctl:ID a control;
   pseltext:I=TEXT (typed, no commit) / pseledit:I=TEXT (typed, then the kill focus); pselpt:X,Y,Z a point event (the AutoCursor
   point); pselent:E[,T[,V]] an entity event; pselesc; pselnamed:ID the list's answer (0 = Cancel); pseldump */
int cui_psel_script(const char *st, const char *arg) {
    if (!strcmp(st, "psel") || !strcmp(st, "pselnw")) { memset(&ps_test_plane, 0, sizeof ps_test_plane); psel_begin(arg && *arg ? arg : NULL, &ps_test_plane, ps_test_done, NULL, !strcmp(st, "pselnw")); return 1; }
    if (!strcmp(st, "pselp") && arg) {
        double *d = &ps_test_plane.o[0]; memset(&ps_test_plane, 0, sizeof ps_test_plane);
        sscanf(arg, "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf", d, d + 1, d + 2, d + 3, d + 4, d + 5, d + 6, d + 7, d + 8);
        psel_begin(NULL, &ps_test_plane, ps_test_done, NULL, 0); return 1;
    }
    if (!strcmp(st, "pselctl") && arg) { if (!psel_ctl(atoi(arg))) printf("    (planesel control %s not taken)\n", arg); return 1; }
    if (!strcmp(st, "pselspin") && arg && strchr(arg, '=')) { psel_spin(atoi(arg), atoi(strchr(arg, '=') + 1)); return 1; }   /* pselspin:I=DELTA */
    if (!strcmp(st, "pselface") && arg) { int si = 0, fi = 0; sscanf(arg, "%d,%d", &si, &fi); if (psel_open && (ps_ev & 0x2000) && si >= 0 && si < F.nsolids) ps_solid_face(si, fi); else printf("    (planesel: no solid face event)\n"); return 1; }   /* pselface:SOLID,FACE */
    if ((!strcmp(st, "pseltext") || !strcmp(st, "pseledit")) && arg && strchr(arg, '=')) {
        int i = atoi(arg); psel_set_text(i, strchr(arg, '=') + 1); if (!strcmp(st, "pseledit")) psel_edit_done(i); return 1;
    }
    if (!strcmp(st, "pselpt") && arg) {
        double w[3] = { 0, 0, 0 }; sscanf(arg, "%lf,%lf,%lf", w, w + 1, w + 2);
        if (!psel_open) return 1;
        if (psel_cue) ps_cue_point(w); else if (ps_ev & 1) ps_point(w); else printf("    (planesel: no point event in mask 0x%x)\n", ps_ev);
        return 1;
    }
    if (!strcmp(st, "pselent") && arg) {
        int e = -1; double T[2] = { 0, 0 }; sscanf(arg, "%d,%lf,%lf", &e, T, T + 1);
        if (!psel_open) return 1;
        if (!(ps_ev & 2) || e < 0 || e >= F.nents || !ps_takes(ent_class(e))) { printf("    (planesel: entity %d not taken, ev 0x%x sel 0x%x)\n", e, ps_ev, ps_sel); return 1; }
        ps_entity(e, NULL, T); return 1;
    }
    if (!strcmp(st, "pselesc")) { psel_esc(); return 1; }
    if (!strcmp(st, "pselnamed") && arg) { psel_named_answer(atoi(arg)); if (pls_which == 4) { pls_open = 0; pls_which = -1; } return 1; }
    if (!strcmp(st, "pseldump")) { psel_dump(); return 1; }
    return 0;
}
