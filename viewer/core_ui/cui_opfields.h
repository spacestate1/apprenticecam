/* cui_opfields.h - the op dialog's template pages (core_ui cui_opdlg.c "template pages"; formats/mcamui_dialogs.md 1).
   A page = the controls of its Mastercam template (exports/<dll>res_dialogs.tsv, positions in dialog units) joined with the
   decoded record fields of formats/opdlg_fields.tsv (tools/gen_opdlg_fields.py -> cui_opfields.inc). Each control shows
   the op's record value (the stored op's record, else the Mill_MM / Mill_Inch defaults op the op is saved from, with
   the op's record patches over it); the controls this program's generators take are live (bound to CamParams, written
   back at OK as record patches only when edited), the others greyed. Both front ends draw pf_ctls at the template
   positions (mcamui opparams_ui.inc page_template, viewer-qt opdlg.cpp OpDialog::buildTemplate). */
#ifndef CUI_OPFIELDS_H
#define CUI_OPFIELDS_H
#include "core_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
/* control kinds: a static text, a picture, an edit, a combo box, a check box, a radio button, a push button, a group
   box, a trackbar, a list, a group box with a check box in its caption (UIGroupbox) */
enum { PFK_STATIC, PFK_PIC, PFK_EDIT, PFK_COMBO, PFK_CHECK, PFK_RADIO, PFK_PUSH, PFK_GROUP, PFK_TRACK, PFK_LIST, PFK_CGROUP };
#define PF_MAXITEMS 48   /* HST Project's source op list (page 1517 control 997) holds the part's ops */
typedef struct {
    int kind, id, x, y, w, h;           /* dialog units inside the page frame (a child panel offset to its host) */
    char text[120];                     /* caption (static / check / radio / button / group); an edit's or combo's shown text */
    int live;                           /* 1: the user can change it here; 0: drawn greyed */
    int on;                             /* check / radio / group check state */
    int nitems, cur;                    /* combo / list: the items and the selected one (-1 none) */
    const char *items[PF_MAXITEMS];
    int nd;                             /* NOT DECODED (greyed; the front ends append the marker) */
    int ours;                           /* a control of this program, not in the template */
    char pic[64];                       /* PFK_PIC: the image file in the icon pack ("" = a blank frame) */
    char tip[200];                      /* a tooltip (the "(ours)" markers: what this program does instead) */
} PfShown;
int pf_page_dialog(int slot);           /* the template dialog of page SLOT for the pending op (0 = not a template page) */
int pf_active(int slot);                /* the page is drawn from its template */
int pf_ctls(PfShown *out, int cap);     /* the shown page's controls in template (draw) order */
void pf_set(int id, double v);          /* an edit committed (v) / a check toggled (v 0/1) / a radio clicked / a combo index */
void pf_reset(void);                    /* a new dialog: no edits */
void pf_apply(CamParams *p);            /* OK: the edited record fields as record patches */
int pf_dirty_sig(unsigned char *out, int cap);   /* the edit state (the Qt dialog's refresh signature) */
#ifdef __cplusplus
}
#endif
#define PF_PAGE_H 295                   /* the page frame's height in dialog units (dialog 6544 frame 3667: pages 295 high) */
#endif
