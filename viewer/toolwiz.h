/* toolwiz - Mastercam 2017's Define Tool / Edit Tool wizard logic (toolmanager.toolwizard / toolmanager.database .NET,
 * spec ../formats/toolmanager_ui.md section 2): per-type geometry fields, the edit-box rules (ValidateEditFieldInput),
 * the corner / shank rules, ToolValidityChecker, auto-naming, and the feed / speed coupling of TlToolViewModel.
 * The UI (mcamui.c) only draws what these tables say. */
#ifndef TOOLWIZ_H
#define TOOLWIZ_H
#include "mcam_load.h"

/* geometry / property fields (one per edit box; the C# box Name is in the comment of tw_field_name) */
enum {
    TWF_NONE = 0,
    TWF_DIA, TWF_OAL, TWF_CUTLEN, TWF_SHLEN, TWF_SHOULDER_DIA, TWF_SHANK_DIA,
    TWF_TIP_DIA, TWF_TAPER_ANGLE, TWF_TIP_ANGLE, TWF_CORNER_R, TWF_CHAMFER,
    TWF_SEC_CORNER_R, TWF_SEC_CHAMFER,
    TWF_SHOULDER_ANGLE, TWF_DRILL_DIA, TWF_DRILL_LEN,
    TWF_THREAD_INFO,          /* inch: threads per inch (stored 1 / pitch), metric: pitch */
    TWF_PROFILE_R, TWF_LOWER_R, TWF_UPPER_R,
    TWF_UPPER_TAPER_C, TWF_LOWER_TAPER_C,   /* shown as 90 - angle (AngleComplimentConverter) */
    TWF_RELIEF,
    TWF_SH_TAPER_ANGLE, TWF_SH_TAPER_LEN, TWF_NECK_DIA, TWF_NECK_LEN,   /* non-cutting geometry view (2.6.x) */
    TWF_COUNT
};

/* a row of the Define page: a group header (field 0, label = group text), an edit box, or a control */
enum { TWR_GROUP, TWR_BOX, TWR_CORNER_TABS, TWR_CORNER_COMBO, TWR_SLOT_CORNERS, TWR_THREAD_FORM, TWR_LEFT_HAND,
       TWR_TAP_TIP, TWR_SHANK_TABS, TWR_STD_SIZES };
typedef struct {
    int kind;
    int field;                /* TWR_BOX: TWF_*; TWR_CORNER_TABS: unused */
    const char *label;
    int advanced;             /* in the AdvancedContent part of its expander */
    int tab;                  /* TWR_BOX inside a corner / shank tab: shown only when that tab is selected (-1 always) */
} TwRow;

typedef struct {              /* wizard state that is not in the tool (TlToolViewModel / page view model fields) */
    int creating;             /* !IsCreating = edit mode */
    int arbor_modified;       /* the shank was edited (stops the shank following the diameter) */
    int chamfer_modified;     /* reamer: the chamfer was edited */
    int name_manual;          /* NameManuallyChanged */
    int lock_corners;         /* slot mill "Lock upper and lower corners" */
    double orig_tip_angle;    /* tap: tip angle when the page opened (plug = 120 if it was 120, else 118) */
    int add_loff, add_doff;   /* AddToLengthOffsetValue / AddToDiamterOffsetValue */
    char seed[64];            /* StandardSizeNamingSeed (set by a standard-size pick, 2.6.y) */
} TwState;

/* tool types offered on page 1, in display order (ToolTypePageView.GenerateModelList) */
typedef struct { int group; const char *label; int type; } TwTypeItem;   /* group 0 Milling, 1 Holemaking */
int tw_type_items(const TwTypeItem **items);
const char *tw_type_label(int type);          /* the page-1 label of a type ("End Mill", ...) */
const char *tw_type_description(int type);    /* ToolTypeList description (DatabaseEnumStrings, used for names) */

/* the Define page rows of a tool type (2.6). Returns the count. */
int tw_rows(int type, const TwRow **rows);
const char *tw_field_name(int field);         /* the C# box Name */
double tw_get(const Tool *t, int field);
/* sets FIELD from an edit box (ToolEditBoxConverter.ConvertBack: < 0 rejected -> 0 returned, value unchanged), then
   applies that page's ValidateEditFieldInput / setter side effects. 1 = accepted. */
int tw_set(Tool *t, TwState *st, int field, double v);
/* corner / shank / combo controls */
int tw_corner_index(const Tool *t);           /* the selected tab / combo index of the corner control of t->type */
void tw_set_corner_index(Tool *t, TwState *st, int index);
int tw_corner_items(int type, const char *const **labels);   /* labels of that control */
int tw_slot_upper_index(const Tool *t);
void tw_set_slot_upper_index(Tool *t, TwState *st, int index);
int tw_shank_possible(const Tool *t, int style);              /* TaperedShankPossible / ReducedNeckPossible */
void tw_set_shank_style(Tool *t, TwState *st, int style);
int tw_box_enabled(const Tool *t, int field);                  /* combo-driven enable states (face / taper mill) */
void tw_set_left_hand(Tool *t, TwState *st, int on);
int tw_tap_tip_index(const Tool *t);
void tw_set_tap_tip_index(Tool *t, TwState *st, int index);

/* ToolValidityChecker.CheckToolValidity: NULL = valid, else the first failing check's message (2.7) */
const char *tw_validate(const Tool *t);
/* auto name (2.8) into OUT when !st->name_manual */
void tw_autoname(Tool *t, const TwState *st);

/* feed / speed coupling (TlToolViewModel, formats/toollib_net.md 6) */
double tw_sfm(const Tool *t);
double tw_fpt(const Tool *t);
void tw_set_sfm(Tool *t, double sfm);
void tw_set_fpt(Tool *t, double fpt);
void tw_set_feed(Tool *t, double feed);
void tw_set_rpm(Tool *t, int rpm);
void tw_set_flutes(Tool *t, int flutes);
void tw_set_dia_speeds(Tool *t, double old_dia);   /* OverallDiameter setter: keep SFM / fpt (not face mills) */
void tw_set_tool_number(Tool *t, const TwState *st, int num);   /* re-derives the D / H offsets (2.3) */

/* type change on leaving page 1: the type's default tool (tool_new), number and offsets (2.3) */
void tw_new_type(Tool *t, TwState *st, int type, int num, int metric);

/* ---- additions (toolwiz.c) ----
   Row conventions: a TWR_BOX with tab >= 0 belongs to the nearest preceding TWR_CORNER_TABS / TWR_SLOT_CORNERS /
   TWR_SHANK_TABS row. TWR_SLOT_CORNERS = the slot mill's "Lock upper and lower corners" CheckBox (tw_set_lock_corners)
   followed by its upper corner tabs (tw_slot_upper_index); the TWR_CORNER_TABS after it is the lower one.
   tw_corner_items / tw_corner_index / tw_set_corner_index on a tap (4/5) = its Tip treatment combo (Plug / Bottoming). */
void tw_init_edit(Tool *t, TwState *st, int add_loff, int add_doff);   /* Edit Tool: state for an existing tool */
void tw_set_name(Tool *t, TwState *st, const char *name);   /* Name setter (sets name_manual unless it is the auto name) */
void tw_set_lock_corners(Tool *t, TwState *st, int on);     /* slot mill LockUpperAndLowerCorners */
int tw_thread_form_items(const char *const **labels);       /* thread mill "Thread form:" items = ThreadShapeList 0..6 */
void tw_set_thread_form(Tool *t, TwState *st, int shape);
const char *tw_thread_info_label(const Tool *t);            /* "Threads per inch:" / "Pitch:" */
const char *tw_box_name(int type, int field);               /* the box's Name in that type's view (NULL = unnamed) */
void tw_format(const Tool *t, int field, char *buf, int n); /* the box text (ToolEditBoxConverter / AngleComplimentConverter) */
/* a standard-size grid row (DropdownGridView.SelectedRow attributes) -> <page>.BulkUpdatePropertiesInternal; sets st->seed */
enum { TWS_OAL = 1, TWS_DIA = 2, TWS_FLUTE_LEN = 4, TWS_THREAD_INFO = 8, TWS_SHANK_DIA = 16, TWS_DRILL_DIA = 32, TWS_DRILL_LEN = 64 };
typedef struct {
    unsigned has;             /* TWS_* attributes present */
    double oal, dia, flute_len, thread_info, shank_dia, drill_dia, drill_len;
    const char *name, *size;  /* "Name" / "Size" attributes (NULL = absent) */
} TwStdRow;
void tw_std_size(Tool *t, TwState *st, const TwStdRow *r);
#endif
