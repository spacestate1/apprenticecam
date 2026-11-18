/* toolwiz - C port of Mastercam 2017's Define Tool / Edit Tool wizard logic (spec: ../formats/toolmanager_ui.md 2.3-2.9).
 *
 * Sources (ILSpy C# under ~/mastercam-re/dotnet/, cited as):
 *   TW/  = toolmanager.toolwizard/   (page view models in ToolManager.ToolWizard.ViewModels.ToolPages/, views' code-behind
 *          ToolManager.ToolWizard.Views.ToolPages/<X>View.cs, bamls views/toolpages/<x>view.baml read with tools/baml_dump.py)
 *   DB/  = toolmanager.database/     (ToolManager.Database.ViewModels/Tl*ViewModel.cs, ValidityChecking/ToolValidityChecker.cs
 *          and ToolConverters.cs, Converters/ToolEditBoxConverter.cs, *.resx)
 *   LOC/ = MastercamExtensions.Localization.dll (~/mastercam-re/files/mastercamextensions.localiza, decompiled with
 *          ilspycmd for EnumExtensions.ToDescription)
 *
 * How the C# is mapped:
 * - A "VM setter" (vm_* / em_* below) is one TlTool*ViewModel property setter, with its change test, its side effects and
 *   the PropertyChanged events it raises (raise()). raise() runs the PropertyObservers the wizard registers on the tool view
 *   model: NonCuttingGeomViewModel's (ctor, NonCuttingGeomViewModel.cs:108-135) and the slot mill corner lock's
 *   (SlotMillPageViewModel.UpdateCornerObservers, SlotMillPageViewModel.cs:101-140). "ModelIsDirty = true" runs the
 *   GeometryPageViewModel observer (GeometryPageViewModel.cs:123-131), i.e. AutoGenerateName (model_dirty()).
 * - tw_set() is the edit box binding: ToolEditBoxConverter.ConvertBack (DB/Converters/ToolEditBoxConverter.cs: a value < 0
 *   is rejected), the bound VM setter, then - only when the view wires that box's SourceUpdated event (the views'
 *   IComponentConnector.Connect) AND the binding has NotifyOnSourceUpdated=True (baml) - the page's
 *   ValidateEditFieldInput(box Name).
 * - Values the entity keeps as nullable (double?) are always present in Tool, so every "HasValue &&" test is taken as true.
 * - Numerics: .NET Math.Round(x) / Math.Round(x, d) / Convert.ToInt32 round half to even (rint under the default rounding
 *   mode); tolerances are the C#'s 5E-05.
 * - Field mapping (coordinator): OverallDiameter=dia, CuttingDepth=flute_len, ShoulderLength=shoulder_len,
 *   OverallLength=oal, ShoulderDiameter=shoulder_dia, ArborDiameter=arbor_dia, CornerRadius=corner_r, TipDiameter=tip_dia,
 *   TipAngle=tip_angle, ThreadPitch=pitch, TaperAngle=taper_angle, ChamferLength=chamfer_len, TlRadiusType=radius_type,
 *   SecondaryRadiusType=sec_radius_type, SecondaryCornerRadius=sec_corner_r, SecondaryChamferDistance=sec_chamfer,
 *   TaperLength=taper_len, NeckDiameter=neck_dia, TlShankType=shank_type, FluteCount=flutes, SpindleSpeed=rpm (kept
 *   positive, spindle_dir 1 = CCW), FeedRate=feed, MCToolType=type. For types 7, 10-19, 21 Mastercam's OpToolInfo +0x20
 *   is TaperAngle and this program reads tip_angle there, so setting taper_angle also sets tip_angle (set_taper()).
 */
#include "toolwiz.h"
#include "toollib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TW_PI 3.14159265358979323846   /* System.Math.PI */
#define TOL 5E-05

/* ===================================================================================================== numerics */

/* .NET Framework 4.x Math.Round(double value, int digits) (MidpointRounding.ToEven): |value| < 1e16 -> value *= 10^digits,
   Math.Round (half to even), value /= 10^digits; otherwise returned unchanged. */
static double net_round(double v, int digits) {
    if (!(fabs(v) < 1e16)) return v;
    double p = 1; for (int i = 0; i < digits; i++) p *= 10;
    return rint(v * p) / p;
}
/* Math.Round(Math.Floor(x * 10000.0) / 10000.0, 4) - "floored to 4 dp" (ToolConverters.cs:311, TaperMillPageViewModel.cs:88 ...) */
static double floor4(double x) { return net_round(floor(x * 10000.0) / 10000.0, 4); }
/* Convert.ToInt32(double): half to even; outside [-2147483648.5, 2147483647.5) (or NaN) throws OverflowException -> *ok = 0 */
static int to_int32(double v, int *ok) {
    if (!(v < 2147483647.5 && v >= -2147483648.5)) { *ok = 0; return 0; }
    *ok = 1; return (int)rint(v);
}
/* DatabaseViewModelBase.ValueChanged(double, double) (DB/ViewModels/DatabaseViewModelBase.cs:58) */
static int vchg(double a, double b) { return fabs(a - b) > TOL; }

/* ToolValidityChecker's comparisons (ToolValidityChecker.cs:11-34) */
static int IsEqualTo(double a, double b) { return fabs(a - b) < TOL; }
static int IsLessThan(double a, double b) { return b - a > TOL; }
static int IsLessThanOrEqualTo(double a, double b) { return a - b < TOL; }
static int IsGreaterThan(double a, double b) { return a - b > TOL; }
static int IsGreaterThanOrEqualTo(double a, double b) { return b - a < TOL; }

/* double.ToString() on .NET Framework 4.5 (toolmanager.*.csproj TargetFramework net45) = "G" = 15 significant digits,
   scientific when the exponent is < -5 or >= 15 (the same switch points as C's %.15G), exponent "E-05"/"E+15". -0 prints
   "0" on .NET Framework. Digit generation of .NET Framework's G15 is not bit-for-bit verified against %.15G (NOT DECODED for
   ties in the 16th digit). */
static void net_g15(double v, char *b, int n) {
    if (isnan(v)) { snprintf(b, n, "NaN"); return; }
    if (isinf(v)) { snprintf(b, n, v > 0 ? "Infinity" : "-Infinity"); return; }
    if (v == 0) { snprintf(b, n, "0"); return; }
    snprintf(b, n, "%.15G", v);
}

/* ===================================================================================================== type families */

/* view model class per type: DB/Services/AssemblyItemViewModelFactory.cs ctor (lines 11-45) */
static int em_vm(int ty) { return ty == 10 || ty == 11 || ty == 12 || ty == 13 || ty == 14 || ty == 15 || ty == 16 || ty == 17 || ty == 18 || ty == 19 || ty == 21; }   /* TlToolEndmillViewModel (14: TlToolSlotMillViewModel derives from it) */
static int drill_vm(int ty) { return ty == 1 || ty == 2 || ty == 3 || ty == 7 || ty == 8 || ty == 9 || ty == 22; }  /* TlToolDrillViewModel and its Boring / CounterBore / CenterDrill subclasses */
static int tap_vm(int ty) { return ty == 4 || ty == 5; }               /* TlToolTapViewModel */
static int thread_vm(int ty) { return ty == 4 || ty == 5 || ty == 24; } /* TlToolThreadingViewModel (+ TlToolThreadMillViewModel) */
/* pages that own a NonCuttingGeomViewModel: EndmillPageViewModel (10/11/19), LollipopMill, ThreadMill, BarrelMill, FeedMill
   (their ctors; ToolWizardPageFactory.cs:17-41 maps types to pages) */
static int has_ncg(int ty) { return ty == 10 || ty == 11 || ty == 19 || ty == 18 || ty == 24 || ty == 25 || ty == 23; }

/* TaperAngle -> also tip_angle for 7, 10-19, 21 (coordinator's OpToolInfo +0x20 rule; for 7 the C# itself does it:
   TlToolBoringViewModel.TaperAngle setter sets base.TipAngle) */
static void set_taper(Tool *t, double v) {
    t->taper_angle = v;
    if (t->type == 7 || (t->type >= 10 && t->type <= 19) || t->type == 21) t->tip_angle = v;
}

/* ===================================================================================================== feed / speed
   TlToolViewModel (DB/ViewModels/TlToolViewModel.cs:835-876) with TlToolTapViewModel's overrides (TlToolTapViewModel.cs:121-144) */
static double k_sfm(const Tool *t) { return t->is_metric ? 318.3 : 3.82; }
static int calc_flutes(const Tool *t) { return tap_vm(t->type) ? 1 : t->flutes; }          /* GetCalculationNumFlutes */
static double calc_sfm(const Tool *t) { return (double)abs(t->rpm) * t->dia / k_sfm(t); }  /* CalculateSfm (SpindleSpeed getter = |x|) */
static double calc_fpt(const Tool *t) {                                                    /* CalculateFpt */
    if (tap_vm(t->type)) return t->pitch;
    double r = 0; int n = calc_flutes(t) * abs(t->rpm);
    if (n > 0) r = t->feed / (double)n;
    return r;
}
static double calc_feed(const Tool *t, double rpm, double fpt) {                           /* CalculateFeedRate */
    if (tap_vm(t->type)) {
        double p = t->is_metric ? t->pitch / 25.4 : t->pitch, f = rpm * p;
        if (f < 0) f *= -1.0;
        if (t->is_metric) f *= 25.4;
        return f;
    }
    return fabs(rpm) * fpt * (double)calc_flutes(t);
}
static int calc_spindle(const Tool *t, double sfm, int *ok) {                              /* CalculateSpindleSpeed */
    *ok = 1;
    if (t->dia < TOL) return 0;
    return to_int32(rint(k_sfm(t) * sfm / t->dia), ok);
}
/* SetSpindleSpeedValue: |value| * (CCW ? -1 : 1); Tool keeps the magnitude in rpm and the direction in spindle_dir */
static void set_rpm_value(Tool *t, int v) { t->rpm = abs(v); }

/* ===================================================================================================== VM plumbing */

typedef struct { Tool *t; TwState *st; int depth; } W;
enum { P_OAL = 1, P_SHLEN, P_ARBOR, P_SHDIA, P_SHANKTYPE, P_RTYPE, P_SECRTYPE, P_CR, P_SECCR, P_CHAMFER, P_SECCHAMFER };
static void raise_pc(W *w, int p);

/* ModelIsDirty = true -> GeometryPageViewModel's observer: UpdateGraphicsWindow + AutoGenerateName (GeometryPageViewModel.cs:123-131) */
static void model_dirty(W *w) { if (w->st) tw_autoname(w->t, w->st); }

/* a plain "if (Entity.X != value) { set; Raise; ModelIsDirty = true; }" setter (TlToolDrillViewModel.TipAngle / ShoulderAngle /
   CornerRadius, TlToolCenterDrillViewModel.DrillDiameter / DrillLength, TlToolReamerViewModel.ChamferLength,
   TlToolBarrelMillViewModel.CornerRadius / ProfileRadius, TlToolFeedMillViewModel.* , TlToolThreadingViewModel.TipAngle) */
static void plain_set(W *w, double *f, double v) { if (*f != v) { *f = v; model_dirty(w); } }

/* TlToolViewModel.OverallLength (TlToolViewModel.cs:33-53) */
static void vm_set_shlen(W *w, double v);
static void vm_set_oal(W *w, double v) {
    Tool *t = w->t; int ty = t->type;
    if (t->oal != v) {
        t->oal = v; raise_pc(w, P_OAL); model_dirty(w);
        if (t->shoulder_len > t->oal && (ty == 1 || ty == 5 || ty == 4 || ty == 7 || ty == 6 || ty == 16 || ty == 14))
            vm_set_shlen(w, fmax(t->oal, t->flute_len));
    }
}
/* TlToolViewModel.ShoulderLength (:76-92) */
static void vm_set_shlen(W *w, double v) { Tool *t = w->t; if (t->shoulder_len != v) { t->shoulder_len = v; raise_pc(w, P_SHLEN); model_dirty(w); } }
/* TlToolViewModel.CuttingDepth (:94-118) */
static void vm_set_cut(W *w, double v) {
    Tool *t = w->t; int ty = t->type;
    if (t->flute_len != v) {
        t->flute_len = v; model_dirty(w);
        if ((ty == 12 || ty == 14 || ty == 16 || ty == 6 || ty == 7 || ty == 5 || ty == 4 || ty == 1) && t->shoulder_len > t->flute_len)
            vm_set_shlen(w, t->flute_len);
    }
}
/* TlToolViewModel.ArborDiameter (:120-136) / ShoulderDiameter (:501-517) */
static void vm_set_arbor(W *w, double v) { Tool *t = w->t; if (t->arbor_dia != v) { t->arbor_dia = v; raise_pc(w, P_ARBOR); model_dirty(w); } }
static void vm_set_shdia(W *w, double v) { Tool *t = w->t; if (t->shoulder_dia != v) { t->shoulder_dia = v; raise_pc(w, P_SHDIA); model_dirty(w); } }
/* TlToolViewModel.MCToolType (:138-154) */
static void vm_set_type(W *w, int v) { if (w->t->type != v) { w->t->type = v; model_dirty(w); } }
/* TlToolViewModel.TaperLength (:583-599), NeckDiameter (:659-674), ShankType (:642-657) */
static void vm_set_taper_len(W *w, double v) { if (w->t->taper_len != v) { w->t->taper_len = v; model_dirty(w); } }
static void vm_set_neck(W *w, double v) { if (fabs(w->t->neck_dia - v) > TOL) { w->t->neck_dia = v; model_dirty(w); } }
static void vm_set_shank_type(W *w, int v) { if (w->t->shank_type != v) { w->t->shank_type = v; raise_pc(w, P_SHANKTYPE); model_dirty(w); } }

/* TlToolViewModel.OverallDiameter (:386-410): keeps SFM and FPT except on face mills. -1 = Convert.ToInt32 overflowed (the
   C# setter throws after OverallDiameter was stored; the binding then raises no SourceUpdated). */
static int vm_set_dia(W *w, double v) {
    Tool *t = w->t;
    if (!vchg(t->dia, v)) return 0;
    double sfm = calc_sfm(t), fpt = calc_fpt(t);
    t->dia = v;
    if (t->type != 13) {
        int ok, r = calc_spindle(t, sfm, &ok);
        if (!ok) return -1;
        t->rpm = abs(r);   /* OpParameters.SpindleSpeed = CalculateSpindleSpeed(sfm) (a magnitude; direction stays in spindle_dir) */
        t->feed = calc_feed(t, t->rpm, fpt);
    }
    model_dirty(w);
    return 0;
}

/* ----- ToolConverters (DB/ValidityChecking/ToolConverters.cs), entity level: no VM side effects ----- */

static void correct_endmill_corner_type(Tool *m) {   /* CorrectEndmillCornerType :309-378 */
    double num = floor4(m->dia / 2.0);
    switch (m->radius_type) {
    case 4: if (m->corner_r == 0.0) { m->type = 10; m->radius_type = 0; } break;
    case 1: if (m->chamfer_len <= 0.0) { m->chamfer_len = 0.0; m->radius_type = 0; } else if (m->chamfer_len >= num) m->chamfer_len = num; break;
    case 0:
        if (m->corner_r > 0.0 && m->corner_r < num) { m->type = 19; m->radius_type = 2; }
        else if (m->corner_r >= num) { m->corner_r = num; m->type = 11; m->radius_type = 3; }
        break;
    case 2:
        if (m->corner_r == 0.0) { m->type = 10; m->radius_type = 0; }
        else if (m->corner_r >= num) { m->corner_r = num; m->type = 11; m->radius_type = 3; }
        break;
    case 3:
        if (m->corner_r == 0.0) { m->type = 10; m->radius_type = 0; break; }
        if (m->corner_r > 0.0 && m->corner_r < num) { m->type = 19; m->radius_type = 2; }
        break;
    }
}
static void correct_slot_corner_type(Tool *m) {     /* CorrectSlotMillCornerType :380-406 */
    double num = floor4(m->flute_len / 2.0);
    switch (m->radius_type) {
    case 1: if (m->chamfer_len <= 0.0) m->chamfer_len = 0.0; else if (m->chamfer_len >= num) m->chamfer_len = num; break;
    case 2: if (m->corner_r <= 0.0) m->corner_r = 0.0; else if (m->corner_r >= num) m->corner_r = num; break;
    }
}
static void correct_taper_corner_type(Tool *m) {    /* CorrectTaperMillCornerType :408-457 */
    double num = floor4(m->dia / 2.0);
    switch (m->radius_type) {
    case 0:
        if (m->corner_r > 0.0 && m->corner_r < num) m->radius_type = 2;
        else if (m->corner_r >= num) { m->corner_r = num; m->radius_type = 3; }
        break;
    case 2:
        if (m->corner_r <= 0.0) { m->corner_r = 0.0; m->radius_type = 0; }
        else if (m->corner_r >= num) { m->corner_r = num; m->radius_type = 3; }
        break;
    case 3:
        if (m->corner_r == 0.0) { m->corner_r = 0.0; m->radius_type = 0; break; }
        if (m->corner_r > 0.0 && m->corner_r < num) m->radius_type = 2;
        break;
    }
}
/* ToolConverters.CorrectCornerTypeOnRadiusChange :6-29 (only TlToolEndmill instances) */
static void correct_corner_type_on_radius_change(Tool *m) {
    if (!em_vm(m->type)) return;
    switch (m->type) {
    case 10: case 11: case 19: correct_endmill_corner_type(m); break;
    case 14: correct_slot_corner_type(m); break;
    case 17: correct_taper_corner_type(m); break;
    }
}
/* ToolConverters.CorrectRadiusOnCornerTypeChange :31-307 */
static void correct_radius_on_corner_type_change(Tool *m) {
    if (!em_vm(m->type)) return;
    switch (m->type) {
    case 10: case 11: case 19:      /* FlatEndmill :39-75, SphericalBallNosedEndmill :76-112, BullNosedEndmill :145-181 (identical) */
        switch (m->radius_type) {
        case 1: m->type = 10; m->corner_r = 0.0; m->chamfer_len = m->dia / 8.0; break;
        case 2: m->type = 19; m->corner_r = m->dia / 4.0; m->chamfer_len = 0.0; break;
        case 3: m->type = 11; m->corner_r = m->dia / 2.0; m->chamfer_len = 0.0; break;
        case 0: m->type = 10; m->corner_r = 0.0; m->chamfer_len = 0.0; break;
        case 4: m->type = 15; m->corner_r = m->dia / 4.0; m->chamfer_len = 0.0; break;
        }
        break;
    case 15:                        /* RoundoverTool :113-144 (chamfer untouched) */
        switch (m->radius_type) {
        case 1: m->type = 10; m->corner_r = 0.0; break;
        case 2: m->type = 19; m->corner_r = m->dia / 4.0; break;
        case 3: m->type = 11; m->corner_r = m->dia / 2.0; break;
        case 0: m->type = 10; m->corner_r = 0.0; break;
        case 4: m->type = 15; m->corner_r = m->dia / 4.0; break;
        }
        break;
    case 13:                        /* FaceMill :182-216 */
        switch (m->radius_type) {
        case 1: set_taper(m, 45.0); m->corner_r = 0.0; break;
        case 2: set_taper(m, 45.0); m->corner_r = m->flute_len / 4.0; break;
        case 3: set_taper(m, 90.0); m->corner_r = m->flute_len; m->tip_dia = m->dia + m->corner_r * 2.0; break;
        case 0: case 4: m->radius_type = 1; set_taper(m, 45.0); m->corner_r = 0.0; break;
        }
        break;
    case 14:                        /* SlottingToolSaw :217-242 */
        switch (m->radius_type) {
        case 1: m->corner_r = 0.0; m->chamfer_len = m->flute_len / 8.0; break;
        case 2: m->corner_r = m->flute_len / 4.0; m->chamfer_len = 0.0; break;
        case 4: m->radius_type = 2; m->corner_r = m->flute_len / 4.0; m->chamfer_len = 0.0; break;
        case 3: m->corner_r = m->flute_len / 2.0; m->chamfer_len = 0.0; break;
        case 0: m->corner_r = 0.0; m->chamfer_len = 0.0; break;
        }
        break;
    case 17:                        /* TaperedEndmill :243-274 */
        switch (m->radius_type) {
        case 1: m->radius_type = 0; m->chamfer_len = 0.0; m->corner_r = 0.0; break;
        case 2: m->corner_r = m->dia / 4.0; m->chamfer_len = 0.0; break;
        case 4: m->radius_type = 0; m->corner_r = 0.0; m->chamfer_len = 0.0; break;
        case 3: m->corner_r = m->dia / 2.0; m->corner_r = floor4(m->corner_r); m->chamfer_len = 0.0; break;
        case 0: m->corner_r = 0.0; m->chamfer_len = 0.0; break;
        }
        break;
    case 16:                        /* DovetailCutter :275-302 */
        switch (m->radius_type) {
        case 1: case 4: case 3: m->radius_type = 0; m->corner_r = 0.0; m->chamfer_len = 0.0; break;
        case 2: m->corner_r = m->flute_len / 4.0; m->chamfer_len = 0.0; break;
        case 0: m->corner_r = 0.0; m->chamfer_len = 0.0; break;
        }
        break;
    default: break;                 /* EndmillChamfer / LollipopCutter :303-305, others: nothing */
    }
}

/* ----- TlToolEndmillViewModel (DB/ViewModels/TlToolEndmillViewModel.cs) ----- */
static void em_set_cr(W *w, double v) {          /* CornerRadius :10-27 */
    Tool *t = w->t;
    if (t->corner_r != v) { t->corner_r = v; correct_corner_type_on_radius_change(t); raise_pc(w, P_CR); raise_pc(w, P_RTYPE); model_dirty(w); }
}
static void em_set_rtype(W *w, int v) {          /* TlRadiusType :34-52 (raises CornerRadius and ChamferLength, not TlRadiusType) */
    Tool *t = w->t;
    if (t->radius_type != v) { t->radius_type = v; correct_radius_on_corner_type_change(t); model_dirty(w); raise_pc(w, P_CR); raise_pc(w, P_CHAMFER); }
}
static void em_set_taper(W *w, double v) {       /* TaperAngle :58-73 */
    if (w->t->taper_angle != v) { set_taper(w->t, v); model_dirty(w); }
}
static int em_set_tip_dia(W *w, double v) {      /* TipDiameter :79-103: on a face mill keeps SFM / FPT */
    Tool *t = w->t;
    if (!vchg(t->tip_dia, v)) return 0;
    double sfm = calc_sfm(t), fpt = calc_fpt(t);
    t->tip_dia = v;
    if (t->type == 13) {
        int ok, r = calc_spindle(t, sfm, &ok);
        if (!ok) return -1;
        t->rpm = abs(r);
        t->feed = calc_feed(t, t->rpm, fpt);
    }
    model_dirty(w);
    return 0;
}
static void em_set_chamfer(W *w, double v) {     /* ChamferLength :109-126 */
    Tool *t = w->t;
    if (t->chamfer_len != v) { t->chamfer_len = v; correct_corner_type_on_radius_change(t); raise_pc(w, P_CHAMFER); raise_pc(w, P_RTYPE); model_dirty(w); }
}

/* ----- TlToolSlotMillViewModel (DB/ViewModels/TlToolSlotMillViewModel.cs) ----- */
static void sl_set_sec_cr(W *w, double v);
static void sl_set_sec_chamfer(W *w, double v);
static void sl_update_cr(W *w) {                 /* UpdateParametersOnCornerRadiusChange */
    Tool *t = w->t;
    if (t->sec_radius_type == 2) {
        double num = floor4(t->flute_len / 2.0);
        if (t->sec_corner_r - num > TOL) sl_set_sec_cr(w, num);
        else if (t->sec_corner_r < 0.0) sl_set_sec_cr(w, 0.0);
    }
}
static void sl_update_chamfer(W *w) {            /* UpdateParametersOnCornerChamferChange */
    Tool *t = w->t;
    if (t->sec_radius_type == 1) {
        double num = floor4(t->flute_len / 2.0);
        if (t->sec_chamfer - num > TOL) sl_set_sec_chamfer(w, num);
        else if (t->sec_chamfer < 0.0) sl_set_sec_chamfer(w, 0.0);
    }
}
static void sl_set_sec_cr(W *w, double v) {      /* SecondaryCornerRadius */
    Tool *t = w->t;
    if (t->sec_corner_r != v) { t->sec_corner_r = v; sl_update_cr(w); raise_pc(w, P_SECCR); model_dirty(w); }
}
static void sl_set_sec_chamfer(W *w, double v) { /* SecondaryChamferDistance */
    Tool *t = w->t;
    if (t->sec_chamfer != v) { t->sec_chamfer = v; sl_update_chamfer(w); raise_pc(w, P_SECCHAMFER); model_dirty(w); }
}
static void sl_set_sec_rtype(W *w, int v) {      /* SecondaryRadiusType + InitCornerParamsOnUpperCornerTypeChange */
    Tool *t = w->t;
    if (t->sec_radius_type != v) {
        t->sec_radius_type = v;
        switch (v) {
        case 0: sl_set_sec_cr(w, 0.0); sl_set_sec_chamfer(w, 0.0); break;
        case 2: sl_set_sec_cr(w, t->flute_len / 4.0); sl_set_sec_chamfer(w, 0.0); break;
        case 1: sl_set_sec_cr(w, 0.0); sl_set_sec_chamfer(w, t->flute_len / 8.0); break;
        }
        raise_pc(w, P_SECRTYPE); model_dirty(w);
    }
}
/* SlotMillPageViewModel.LowerCornerType / UpperCornerType setters (SlotMillPageViewModel.cs:30-64) */
static void slot_set_lower(W *w, int v) { if (w->t->radius_type != v) em_set_rtype(w, v); }
static void slot_set_upper(W *w, int v) { if (w->t->sec_radius_type != v) sl_set_sec_rtype(w, v); }

/* ----- TlToolThreadingViewModel.ThreadPitch (TlToolThreadingViewModel.cs:42-63) ----- */
static void th_set_pitch(W *w, double v) {
    Tool *t = w->t;
    if (vchg(t->pitch, v)) {
        t->pitch = v;
        if (t->type != 24) t->feed = calc_feed(t, abs(t->rpm), v);
        model_dirty(w);
    }
}
/* ----- TlToolBoringViewModel (TlToolBoringViewModel.cs) ----- */
static void bo_set_taper(W *w, double v) {       /* TaperAngle: base.TipAngle = value first */
    Tool *t = w->t;
    if (t->taper_angle != v) { plain_set(w, &t->tip_angle, v); set_taper(t, v); model_dirty(w); }
}
static void bo_set_rtype(W *w, int v) {          /* TlRadiusType: CornerRadius = 0 or CuttingDepth / 4 (no ModelIsDirty) */
    Tool *t = w->t;
    if (t->radius_type != v) { t->radius_type = v; t->corner_r = v == 0 ? 0.0 : t->flute_len / 4.0; }
}

/* ===================================================================================================== NonCuttingGeomViewModel
   TW/ViewModels.ToolPages/NonCuttingGeomViewModel.cs */
static double ncg_taper_angle(const Tool *t) {   /* GetTaperAngle :236-244 */
    double num = fabs(t->arbor_dia - t->shoulder_dia) / 2.0;
    if (!(num < TOL)) return net_round(atan(num / t->taper_len) * (180.0 / TW_PI), 4);
    return 90.0;
}
static double ncg_len_from_angle(const Tool *t, double angle) {   /* GetTaperLengthFromAngle :246-259 */
    double num = (t->arbor_dia - t->shoulder_dia) / 2.0, num2 = t->oal - t->shoulder_len;
    if (num > TOL && angle < net_round(atan(num / num2) * (180.0 / TW_PI), 4)) return num2;
    if (num < TOL || angle >= 90.0) return 0.0;
    return num / tan(angle * (TW_PI / 180.0));
}
static int ncg_tapered_possible(const Tool *t) { /* TaperedShankPossible :81-93 */
    double num = (t->arbor_dia - t->shoulder_dia) / 2.0, num2 = t->oal - t->shoulder_len;
    return num > TOL ? num2 > TOL : 0;
}
static int ncg_neck_possible(const Tool *t) {    /* ReducedNeckPossible :95-106 */
    double num2 = fmin(t->arbor_dia, t->shoulder_dia), num3 = t->oal - t->shoulder_len;
    double num4 = (t->arbor_dia - num2) / 2.0, num5 = (t->neck_dia - num2) / 2.0;
    return num3 - (num4 + num5) > TOL;
}
static void ncg_auto_adjust(W *w) {              /* AutoAdjustTaper :137-188 */
    Tool *t = w->t;
    switch (t->shank_type) {
    case 0: vm_set_taper_len(w, 0.0); vm_set_neck(w, t->shoulder_dia); break;
    case 1: {
        double num2 = (t->arbor_dia - t->shoulder_dia) / 2.0, num3 = t->oal - t->shoulder_len;
        if (num2 < TOL || t->taper_len < TOL) { vm_set_taper_len(w, 0.0); vm_set_shank_type(w, 0); }
        else if (t->taper_len > num3 && num3 >= 0.0) vm_set_taper_len(w, num3);
        break;
    }
    case 2: {
        double num = fmin(t->arbor_dia, t->shoulder_dia);
        if (t->neck_dia - num > TOL) vm_set_neck(w, num);
        double value = t->arbor_dia - t->shoulder_dia;
        if ((fabs(value) < TOL && fabs(t->arbor_dia - t->neck_dia) < TOL) || t->taper_len < TOL) { vm_set_taper_len(w, 0.0); vm_set_shank_type(w, 0); }
        break;
    }
    }
}
static void ncg_init_type(W *w) {                /* InitializeValuesOnTypeChange :190-234 */
    Tool *t = w->t;
    switch (t->shank_type) {
    case 0: vm_set_taper_len(w, 0.0); vm_set_neck(w, t->shoulder_dia); break;
    case 1: {
        double num6 = (t->arbor_dia - t->shoulder_dia) / 2.0, num7 = t->oal - t->shoulder_len;
        vm_set_taper_len(w, num6 > num7 ? num7 : num6);
        break;
    }
    case 2: {
        double num = fmin(t->arbor_dia, t->shoulder_dia), num2 = num * 0.9;
        double num3 = (t->arbor_dia - num2) / 2.0, num4 = (t->neck_dia - num2) / 2.0, num5 = (t->oal - t->shoulder_len) / 3.0;
        vm_set_neck(w, num2);
        vm_set_taper_len(w, num5 - (num3 + num4) < TOL ? num3 + num4 : num5);
        break;
    }
    }
}

/* PropertyChanged dispatch to the observers that exist while the wizard is open. The depth guard is not in the C# (there an
   endless observer loop would overflow the stack); the equality tests in the setters end every chain seen in testing. */
static void raise_pc(W *w, int p) {
    Tool *t = w->t;
    if (w->depth > 64) return;
    w->depth++;
    if (has_ncg(t->type)) {                     /* NonCuttingGeomViewModel ctor :108-135 */
        if (p == P_OAL || p == P_SHLEN || p == P_ARBOR || p == P_SHDIA) ncg_auto_adjust(w);
        else if (p == P_SHANKTYPE) ncg_init_type(w);
    }
    if (t->type == 14 && w->st && w->st->lock_corners) {   /* SlotMillPageViewModel.UpdateCornerObservers :101-130 */
        switch (p) {
        case P_SECRTYPE: slot_set_lower(w, t->sec_radius_type); break;
        case P_RTYPE: slot_set_upper(w, t->radius_type); break;
        case P_SECCHAMFER: em_set_chamfer(w, t->sec_chamfer); break;
        case P_CHAMFER: sl_set_sec_chamfer(w, t->chamfer_len); break;
        case P_SECCR: em_set_cr(w, t->sec_corner_r); break;
        case P_CR: sl_set_sec_cr(w, t->corner_r); break;
        }
    }
    w->depth--;
}

/* ===================================================================================================== page 1: tool types */

/* ToolTypePageView.GenerateModelList (TW/Views.ToolPages/ToolTypePageView.cs:43-330), labels ToolTypePageViewXaml.resx */
static const TwTypeItem type_items[] = {
    {0, "End Mill", 10}, {0, "Bull Mill", 19}, {0, "Ball Mill", 11}, {0, "Face Mill", 13}, {0, "Radius Mill", 15},
    {0, "Chamfer Mill", 12}, {0, "Slot Mill", 14}, {0, "Taper Mill", 17}, {0, "Dove Mill", 16}, {0, "Lollipop Mill", 18},
    {0, "Engrave Tool", 21}, {0, "Thread Mill", 24}, {0, "Barrel Mill", 25}, {0, "High Feed Mill", 23}, {0, "Custom Tool", 0},
    {1, "Drill", 3}, {1, "Spot Drill", 2}, {1, "Center Drill", 1}, {1, "Reamer", 6}, {1, "Countersink", 9},
    {1, "Counterbore", 8}, {1, "Tap", 4}, {1, "Bradpoint Drill", 22}, {1, "Bore Bar", 7}, {1, "Custom Tool", 0},
};
int tw_type_items(const TwTypeItem **items) { *items = type_items; return (int)(sizeof type_items / sizeof *type_items); }

/* the page-1 label; type 5 (not on page 1) uses the Tap page's label (TapDrillPageViewModel ctor title,
   ToolWizardPageFactory.cs:21 maps 5 to that page) */
const char *tw_type_label(int type) {
    if (type == 5) return "Tap";
    for (size_t i = 0; i < sizeof type_items / sizeof *type_items; i++) if (type_items[i].type == type) return type_items[i].label;
    return NULL;
}

/* ToolTypeList_* in DB/ToolManager.Database.Resources.DatabaseEnumStrings.resx (attributes in DB/Resources/ToolTypeList.cs) */
const char *tw_type_description(int type) {
    switch (type) {
    case 0: return "Custom Tool"; case 1: return "Center Drill"; case 2: return "Spot Drill"; case 3: return "Standard Drill";
    case 4: return "Right-Handed Tap"; case 5: return "Left-Handed Tap"; case 6: return "Reamer Type"; case 7: return "Fine Boring Tool";
    case 8: return "Counter Bore"; case 9: return "Counter Sink"; case 10: return "Flat Endmill"; case 11: return "Spherical / Ball-Nosed Endmill";
    case 12: return "Chamfer Mill"; case 13: return "Face Mill / Shell Mill"; case 14: return "Slotting Tool or Saw"; case 15: return "Roundover Tool";
    case 16: return "Dovetail Cutter"; case 17: return "Tapered Endmill"; case 18: return "Lollipop Cutter"; case 19: return "Bull-Nosed Endmill";
    case 21: return "Engraving Bit"; case 22: return "Brad-Point Drill Bit"; case 23: return "High Feed Mill"; case 24: return "Thread Mill";
    case 25: return "Barrel Mill";
    }
    return NULL;
}

/* ===================================================================================================== Define page rows
   One table per view baml (TW/views/toolpages/<x>view.baml); labels are the English resx texts of each TextBlock /
   expander HeaderText / CheckBox Content. A box with tab >= 0 belongs to the nearest preceding TWR_CORNER_TABS /
   TWR_SLOT_CORNERS / TWR_SHANK_TABS row. TWR_SLOT_CORNERS = the slot mill's "Lock upper and lower corners" CheckBox followed
   by the upper corner ButtonTabControl; the lower one is the TWR_CORNER_TABS after it. */
#define G(l) {TWR_GROUP, 0, l, 0, -1}
#define B(f, l) {TWR_BOX, f, l, 0, -1}
#define BA(f, l) {TWR_BOX, f, l, 1, -1}
#define BT(f, l, tb) {TWR_BOX, f, l, 0, tb}
#define K(k, l) {k, 0, l, 0, -1}
#define OVERALL3(dl) G("Overall dimensions"), B(TWF_DIA, dl), B(TWF_OAL, "Overall length:"), B(TWF_CUTLEN, "Cutting length:")
#define NCG_ROWS G("Non-cutting geometry"), B(TWF_SHLEN, "Shoulder length:"), B(TWF_SHOULDER_DIA, "Shoulder diameter:"), \
    B(TWF_SHANK_DIA, "Shank diameter:"), K(TWR_SHANK_TABS, ""), BT(TWF_SH_TAPER_ANGLE, "Taper angle:", 1), \
    BT(TWF_SH_TAPER_LEN, "Taper length:", 1), BT(TWF_NECK_DIA, "Neck diameter:", 2), BT(TWF_NECK_LEN, "Neck length:", 2)
#define NONCUT2 G("Non-cutting geometry"), B(TWF_SHLEN, "Shoulder length:"), B(TWF_SHANK_DIA, "Shank diameter:")

static const TwRow rows_endmill[] = {    /* endmillview.baml + noncuttinggeometryview.baml */
    OVERALL3("Cutting diameter:"), G("Tip / corner treatment"), K(TWR_CORNER_TABS, ""),
    BT(TWF_CHAMFER, "Chamfer distance:", 1), BT(TWF_CORNER_R, "Radius:", 2), NCG_ROWS };
static const TwRow rows_face[] = {       /* facemillview.baml */
    G("Overall dimensions"), B(TWF_DIA, "Cutting diameter:"), B(TWF_TIP_DIA, "Secondary diameter:"), B(TWF_OAL, "Overall length:"),
    B(TWF_CUTLEN, "Cutting length:"), G("Tip / corner treatment"), K(TWR_CORNER_COMBO, "Corner type:"),
    B(TWF_TAPER_ANGLE, "Chamfer angle:"), B(TWF_CORNER_R, "Radius:"),
    G("Non-cutting geometry"), B(TWF_SHLEN, "Shoulder length:"), B(TWF_SHANK_DIA, "Upper diameter:") };
static const TwRow rows_radius[] = {     /* radiusmillview.baml */
    OVERALL3("Tip diameter:"), G("Tip / corner treatment"), B(TWF_CORNER_R, "Radius:"), NONCUT2 };
static const TwRow rows_chamfer[] = {    /* chamfermillview.baml */
    OVERALL3("Outer diameter:"), G("Tip / corner treatment"), B(TWF_TAPER_ANGLE, "Chamfer angle:"), B(TWF_TIP_DIA, "Tip diameter:"),
    BA(TWF_CORNER_R, "Corner radius:"), NONCUT2 };
static const TwRow rows_slot[] = {       /* slotmillview.baml */
    G("Overall dimensions"), B(TWF_DIA, "Cutting diameter:"), B(TWF_OAL, "Overall length:"), B(TWF_CUTLEN, "Thickness:"),
    G("Tip / corner treatment"), K(TWR_SLOT_CORNERS, "Lock upper and lower corners"),
    BT(TWF_SEC_CHAMFER, "Chamfer distance:", 1), BT(TWF_SEC_CORNER_R, "Radius:", 2), K(TWR_CORNER_TABS, ""),
    BT(TWF_CHAMFER, "Chamfer distance:", 1), BT(TWF_CORNER_R, "Radius:", 2),
    G("Non-cutting geometry"), B(TWF_SHANK_DIA, "Shank diameter:"), BA(TWF_SHLEN, "Shoulder length:") };
static const TwRow rows_taper[] = {      /* tapermillview.baml */
    G("Overall dimensions"), B(TWF_DIA, "Tip diameter:"), B(TWF_TAPER_ANGLE, "Angle:"), B(TWF_CUTLEN, "Cutting length:"),
    B(TWF_SHLEN, "Shoulder length:"), B(TWF_OAL, "Overall length:"), G("Tip / corner treatment"), K(TWR_CORNER_COMBO, "Corner type:"),
    B(TWF_CORNER_R, "Radius:"), G("Non-cutting geometry"), B(TWF_SHANK_DIA, "Shank diameter:") };
static const TwRow rows_dove[] = {       /* dovemillview.baml */
    OVERALL3("Cutting diameter:"), B(TWF_TAPER_ANGLE, "Taper angle:"), G("Tip / corner treatment"), K(TWR_CORNER_TABS, ""),
    BT(TWF_CORNER_R, "Radius:", 1), G("Non-cutting geometry"), B(TWF_SHANK_DIA, "Shank diameter:"), BA(TWF_SHLEN, "Shoulder length:") };
static const TwRow rows_lollipop[] = {   /* lollipopmillview.baml */
    OVERALL3("Cutting diameter:"), NCG_ROWS };
static const TwRow rows_engrave[] = {    /* engravemillview.baml */
    OVERALL3("Outer diameter:"), G("Tip / corner treatment"), B(TWF_TAPER_ANGLE, "Angle:"), B(TWF_TIP_DIA, "Tip diameter:"), NONCUT2 };
static const TwRow rows_thread[] = {     /* threadmillview.baml (ThreadInfoLabel: tw_thread_info_label) */
    G("Overall dimensions"), K(TWR_THREAD_FORM, "Thread form:"), B(TWF_THREAD_INFO, "Threads per inch:"),
    B(TWF_DIA, "Outside diameter:"), B(TWF_OAL, "Overall length:"), B(TWF_CUTLEN, "Cutting length:"), NCG_ROWS };
static const TwRow rows_barrel[] = {     /* barrelmillview.baml */
    OVERALL3("Cutting diameter:"), B(TWF_PROFILE_R, "Profile radius:"), B(TWF_CORNER_R, "Corner radius:"), NCG_ROWS };
static const TwRow rows_feed[] = {       /* feedmillview.baml */
    OVERALL3("Cutting diameter:"), G("Cutting geometry"), B(TWF_TIP_DIA, "Tip diameter:"), B(TWF_UPPER_TAPER_C, "Upper taper angle:"),
    B(TWF_LOWER_R, "Lower radius:"), B(TWF_UPPER_R, "Upper radius:"), B(TWF_RELIEF, "Relief taper angle:"),
    BA(TWF_LOWER_TAPER_C, "Lower taper angle:"), NCG_ROWS };
static const TwRow rows_custom[] = {     /* customdxfpageview.baml */
    OVERALL3("Cutting diameter:"), NONCUT2 };
static const TwRow rows_spot[] = {       /* spotdrillview.baml */
    G("Standard sizes"), K(TWR_STD_SIZES, ""), OVERALL3("Drill diameter:"), G("Tip treatment"), B(TWF_TIP_ANGLE, "Tip angle:"), NONCUT2 };
static const TwRow rows_center[] = {     /* centerdrillview.baml */
    G("Standard sizes"), K(TWR_STD_SIZES, ""), G("Overall dimensions"), B(TWF_CUTLEN, "Cutting length:"),
    B(TWF_SHANK_DIA, "Shank diameter:"), B(TWF_OAL, "Overall length:"), G("Tip treatment"), B(TWF_DRILL_DIA, "Drill diameter:"),
    B(TWF_DRILL_LEN, "Drill length:"), B(TWF_TIP_ANGLE, "Drill angle:"), B(TWF_SHOULDER_ANGLE, "Shoulder angle:") };
static const TwRow rows_reamer[] = {     /* reamerview.baml */
    OVERALL3("Cutting diameter:"), G("Tip / corner treatment"), B(TWF_CHAMFER, "Chamfer distance:"),
    G("Non-cutting geometry"), B(TWF_SHANK_DIA, "Shank diameter:"), BA(TWF_SHLEN, "Shoulder length:") };
static const TwRow rows_csink[] = {      /* countersinkview.baml */
    OVERALL3("Outer diameter:"), G("Tip / corner treatment"), B(TWF_TIP_ANGLE, "Tip angle (included):"), NONCUT2 };
static const TwRow rows_cbore[] = {      /* counterboreview.baml */
    OVERALL3("Cutting diameter:"), NONCUT2 };
static const TwRow rows_tap[] = {        /* tapdrillview.baml (ThreadPerTextBlock: DataTrigger on IsMetric, tw_thread_info_label) */
    G("Standard sizes"), K(TWR_STD_SIZES, ""), B(TWF_DIA, "Nominal diameter:"), B(TWF_THREAD_INFO, "Threads per inch:"),
    K(TWR_LEFT_HAND, "Left hand"), G("Overall dimensions"), B(TWF_CUTLEN, "Cutting length:"), B(TWF_SHANK_DIA, "Shank diameter:"),
    B(TWF_OAL, "Overall length:"), G("Tip treatment"), K(TWR_TAP_TIP, ""), BA(TWF_SHLEN, "Shoulder length:") };
static const TwRow rows_brad[] = {       /* bradpointdrillview.baml */
    G("Standard sizes"), K(TWR_STD_SIZES, ""), OVERALL3("Drill diameter:"), NONCUT2 };
static const TwRow rows_bore[] = {       /* boringview.baml */
    OVERALL3("Cutting diameter:"), B(TWF_TAPER_ANGLE, "Taper angle:"), G("Tip / corner treatment"), K(TWR_CORNER_TABS, ""),
    BT(TWF_CORNER_R, "Radius:", 1), G("Non-cutting geometry"), B(TWF_SHANK_DIA, "Shank diameter:"), BA(TWF_SHLEN, "Shoulder length:") };

#define RET(a) do { *rows = a; return (int)(sizeof a / sizeof *a); } while (0)
/* view per type: ToolWizardPageFactory.cs:17-41 + the DataTemplates of tooldefinepageview.baml */
int tw_rows(int type, const TwRow **rows) {
    switch (type) {
    case 10: case 11: case 19: RET(rows_endmill);
    case 13: RET(rows_face); case 15: RET(rows_radius); case 12: RET(rows_chamfer); case 14: RET(rows_slot);
    case 17: RET(rows_taper); case 16: RET(rows_dove); case 18: RET(rows_lollipop); case 21: RET(rows_engrave);
    case 24: RET(rows_thread); case 25: RET(rows_barrel); case 23: RET(rows_feed); case 0: RET(rows_custom);
    case 2: case 3: RET(rows_spot); case 1: RET(rows_center); case 6: RET(rows_reamer); case 9: RET(rows_csink);
    case 8: RET(rows_cbore); case 4: case 5: RET(rows_tap); case 22: RET(rows_brad); case 7: RET(rows_bore);
    }
    *rows = NULL; return 0;
}
static int on_page(int type, int field) {
    const TwRow *r; int n = tw_rows(type, &r);
    for (int i = 0; i < n; i++) if (r[i].kind == TWR_BOX && r[i].field == field) return 1;
    return 0;
}

/* the generic C# box Name of a field (tw_box_name gives the per-view one; NULL = the box has no Name) */
const char *tw_field_name(int field) {
    switch (field) {
    case TWF_DIA: return "CuttingDiameter"; case TWF_OAL: return "OverallLength"; case TWF_CUTLEN: return "CuttingLength";
    case TWF_SHLEN: return "ShoulderLength"; case TWF_SHOULDER_DIA: return "ShoulderDiameter"; case TWF_SHANK_DIA: return "ShankDiameter";
    case TWF_TIP_DIA: return "TipDiameter"; case TWF_TAPER_ANGLE: return "TaperAngle"; case TWF_TIP_ANGLE: return "TipAngle";
    case TWF_CORNER_R: return "CornerRadius"; case TWF_SHOULDER_ANGLE: return "ShoulderAngle"; case TWF_DRILL_DIA: return "CenterDrillDiameter";
    case TWF_DRILL_LEN: return "CenterDrillLength"; case TWF_THREAD_INFO: return "ThreadPer"; case TWF_PROFILE_R: return "ProfileRadius";
    }
    return NULL;
}
/* the Name of FIELD's box in TYPE's view (bamls; NULL = unnamed box) */
const char *tw_box_name(int type, int field) {
    switch (field) {
    case TWF_TIP_DIA: return type == 23 ? NULL : "TipDiameter";
    case TWF_TAPER_ANGLE:
        switch (type) { case 13: return "TaperAngleFaceMill"; case 12: return "TaperAngleChamferMill"; case 17: return "TaperAngle";
                        case 16: return "TaperAngleDoveMill"; case 21: return "TaperAngleEngraveMill"; case 7: return "TaperAngleBoring"; }
        return NULL;
    case TWF_CORNER_R:
        switch (type) { case 13: return "FacemillRadius"; case 15: return "RadMillRadius"; case 12: case 17: case 25: return "CornerRadius"; }
        return NULL;
    case TWF_CHAMFER: return type == 6 ? "ChamferDistanceDrill" : NULL;
    }
    return tw_field_name(field);
}
/* boxes whose SourceUpdated calls ValidateEditFieldInput: wired in the view's Connect (TW/Views.ToolPages/<View>.cs) AND bound
   with NotifyOnSourceUpdated=True (baml). The NonCuttingGeometryView boxes (NonCuttingGeometryView.cs TextBoxUpdated) count
   for the pages that host it. ThreadMillView and FeedMillView wire none of their own boxes. */
static int wired(int type, const char *box) {
    const char *l = "";
    switch (type) {
    case 10: case 11: case 19: l = " CuttingDiameter OverallLength CuttingLength "; break;
    case 13: l = " TipDiameter CuttingLength FacemillRadius "; break;
    case 15: l = " CuttingDiameter CuttingLength RadMillRadius "; break;
    case 12: case 21: l = " CuttingDiameter CuttingLength ShankDiameter "; break;
    case 14: case 16: l = " CuttingLength ShoulderLength "; break;
    case 17: l = " CuttingDiameter TaperAngle CuttingLength CornerRadius "; break;
    case 18: l = " CuttingDiameter OverallLength CuttingLength "; break;
    case 25: l = " CuttingDiameter OverallLength CuttingLength ProfileRadius CornerRadius "; break;
    case 0: case 7: case 8: case 9: l = " CuttingLength "; break;
    case 2: case 3: l = " CuttingDiameter OverallLength CuttingLength TipAngle ShankDiameter "; break;
    case 1: l = " CuttingLength ShankDiameter CenterDrillDiameter CenterDrillLength TipAngle ShoulderAngle "; break;
    case 6: l = " CuttingDiameter OverallLength CuttingLength ChamferDistanceDrill ShankDiameter ShoulderLength "; break;
    case 4: case 5: l = " CuttingDiameter LeftHand CuttingLength ShankDiameter ShoulderLength ThreadPer "; break;
    case 22: l = " CuttingDiameter OverallLength CuttingLength ShankDiameter "; break;
    }
    char k[64]; snprintf(k, sizeof k, " %s ", box);
    if (strstr(l, k)) return 1;
    return has_ncg(type) && (!strcmp(box, "ShoulderLength") || !strcmp(box, "ShoulderDiameter") || !strcmp(box, "ShankDiameter"));
}

/* ===================================================================================================== ValidateEditFieldInput */

static void shoulder_ge_cut(W *w) { if (w->t->flute_len > w->t->shoulder_len) vm_set_shlen(w, w->t->flute_len); }
/* CenterDrillPageViewModel.CalculateShoulderLength (CenterDrillPageViewModel.cs:88-93) */
static double center_calc(const Tool *t) {
    double num = (t->dia - t->drill_dia) / 2.0;
    return num * tan((90.0 - t->shoulder_angle / 2.0) * (TW_PI / 180.0)) + t->drill_len;
}
static void page_validate(W *w, const char *f) {
    Tool *t = w->t; TwState *st = w->st;
    int creating = st ? st->creating : 0;
#define IS(s) (!strcmp(f, s))
    switch (t->type) {
    case 10: case 11: case 19:      /* EndmillPageViewModel.ValidateEditFieldInput :69-111 */
        if (IS("CuttingDiameter")) {
            if (st && !st->arbor_modified && creating) { vm_set_shdia(w, t->dia); vm_set_arbor(w, t->dia); }
            if (t->radius_type == 3) { em_set_cr(w, t->dia / 2.0); break; }
            /* creating: ToolConverters.CorrectRadiusOnCornerTypeChange on the entity (no VM events); the page's
               RaisePropertyChanged("ModelIsDirty") that follows only refreshes the page bindings */
            if (creating) correct_radius_on_corner_type_change(t);
        } else if (IS("ShankDiameter") || IS("ShoulderDiameter")) { if (st) st->arbor_modified = 1; }
        else if (IS("CuttingLength")) shoulder_ge_cut(w);
        break;
    case 13:                        /* FacemillPageViewModel :69-90 */
        if (IS("TipDiameter") || IS("FacemillRadius")) { if (t->radius_type == 3) vm_set_dia(w, t->tip_dia - t->corner_r * 2.0); }
        else if (IS("CuttingLength")) shoulder_ge_cut(w);
        break;
    case 17:                        /* TaperMillPageViewModel :72-125 */
        if (IS("TaperAngle") || IS("CuttingDiameter")) {
            if (t->radius_type == 3) { double num2 = t->dia / 2.0; em_set_cr(w, num2); em_set_cr(w, floor4(num2)); break; }
            if (creating) correct_radius_on_corner_type_change(t);
        } else if (IS("CornerRadius")) {
            if (t->radius_type == 3) vm_set_dia(w, floor4(t->corner_r * 2.0));
            else { double num3 = floor4(t->dia / 2.0); if (t->corner_r > num3) em_set_cr(w, num3); }
        } else if (IS("CuttingLength")) shoulder_ge_cut(w);
        break;
    case 14: case 16: case 7: case 4: case 5:   /* SlotMill :92-99, DoveMill :60-68, Boring :60-67, TapDrill :117-125 */
        if (IS("CuttingLength")) vm_set_shlen(w, t->flute_len);
        break;
    case 18:                        /* LollipopMillPageViewModel :46-61 */
        if (IS("CuttingDiameter")) em_set_cr(w, t->dia / 2.0);
        else if (IS("CuttingLength")) shoulder_ge_cut(w);
        break;
    case 21: case 2: case 3: case 22:   /* EngraveMill :32-54, SpotDrill :38-60, BradPointDrill :36-58 */
        if (IS("CuttingDiameter")) { if (st && !st->arbor_modified && creating) { vm_set_shdia(w, t->dia); vm_set_arbor(w, t->dia); } }
        else if (IS("ShankDiameter")) { if (st) st->arbor_modified = 1; }
        else if (IS("CuttingLength")) shoulder_ge_cut(w);
        break;
    case 25:                        /* BarrelMillPageViewModel :32-43 (BarrelLength setter: no ModelIsDirty) */
        if (IS("CuttingLength")) { shoulder_ge_cut(w); t->barrel_len = t->flute_len; }
        break;
    case 1:                         /* CenterDrillPageViewModel :33-53 */
        if (IS("CuttingLength")) vm_set_shlen(w, t->flute_len + 0.005);
        else if (IS("ShankDiameter") || IS("CenterDrillLength") || IS("CenterDrillDiameter") || IS("ShoulderAngle")) {
            if (IS("ShankDiameter")) vm_set_dia(w, t->arbor_dia);
            vm_set_cut(w, net_round(center_calc(t), t->is_metric ? 5 : 6));
            vm_set_shlen(w, t->flute_len + 0.005);
        }
        break;
    case 6:                         /* ReamerPageViewModel :33-55 */
        if (IS("CuttingLength")) vm_set_shlen(w, t->flute_len);
        else if (IS("CuttingDiameter")) { if (creating && st && !st->chamfer_modified) plain_set(w, &t->chamfer_len, net_round(t->dia / 8.0, t->is_metric ? 5 : 6)); }
        else if (IS("ChamferDistanceDrill")) {
            if (st) st->chamfer_modified = 1;
            if (t->chamfer_len > t->dia / 2.0) plain_set(w, &t->chamfer_len, net_round(t->dia / 2.0, t->is_metric ? 5 : 6));
        }
        break;
    case 12: case 15: case 9: case 8: case 0: case 24:   /* ChamferMill, RadiusMill, CounterSink, Counterbore, CustomTool,
                                                            ThreadMill (its CuttingLength box is not wired: never reached) */
        if (IS("CuttingLength")) shoulder_ge_cut(w);
        break;
    }
#undef IS
}

/* ===================================================================================================== fields */

double tw_get(const Tool *t, int field) {
    switch (field) {
    case TWF_DIA: return t->dia; case TWF_OAL: return t->oal; case TWF_CUTLEN: return t->flute_len;
    case TWF_SHLEN: return t->shoulder_len; case TWF_SHOULDER_DIA: return t->shoulder_dia; case TWF_SHANK_DIA: return t->arbor_dia;
    case TWF_TIP_DIA: return t->tip_dia; case TWF_TAPER_ANGLE: return t->taper_angle; case TWF_TIP_ANGLE: return t->tip_angle;
    case TWF_CORNER_R: return t->corner_r; case TWF_CHAMFER: return t->chamfer_len;
    case TWF_SEC_CORNER_R: return t->sec_corner_r; case TWF_SEC_CHAMFER: return t->sec_chamfer;
    case TWF_SHOULDER_ANGLE: return t->shoulder_angle; case TWF_DRILL_DIA: return t->drill_dia; case TWF_DRILL_LEN: return t->drill_len;
    case TWF_THREAD_INFO:           /* ThreadMillPageViewModel / TapDrillPageViewModel .ThreadInfo getter */
        if (!t->is_metric) return t->pitch == 0.0 ? 0.0 : 1.0 / t->pitch;
        return t->pitch;
    case TWF_PROFILE_R: return t->profile_r; case TWF_LOWER_R: return t->lower_r; case TWF_UPPER_R: return t->upper_r;
    case TWF_UPPER_TAPER_C: return 90.0 - t->upper_taper;   /* AngleComplimentConverter.Convert */
    case TWF_LOWER_TAPER_C: return 90.0 - t->lower_taper;
    case TWF_RELIEF: return t->relief_taper;
    case TWF_SH_TAPER_ANGLE: return ncg_taper_angle(t);
    case TWF_SH_TAPER_LEN: case TWF_NECK_LEN: return t->taper_len;
    case TWF_NECK_DIA: return t->neck_dia;
    }
    return 0;
}

/* the box text: ToolEditBoxConverter.Convert = Math.Round(v, IsMetric ? 5 : 6) -> Convert.ToString(InvariantCulture)
   (IsMetric is the tool's, WizardPageViewModel.cs:49); AngleComplimentConverter.Convert returns 90 - v unrounded */
void tw_format(const Tool *t, int field, char *buf, int n) {
    double v = tw_get(t, field);
    if (field == TWF_UPPER_TAPER_C || field == TWF_LOWER_TAPER_C) net_g15(v, buf, n);
    else net_g15(net_round(v, t->is_metric ? 5 : 6), buf, n);
}

int tw_set(Tool *t, TwState *st, int field, double v) {
    if (v < 0.0) return 0;                       /* ToolEditBoxConverter.ConvertBack: < 0 -> UnsetValue */
    if (!on_page(t->type, field)) return 0;
    W w = {t, st, 0};
    int rc = 0, ty = t->type;
    switch (field) {
    case TWF_DIA: rc = vm_set_dia(&w, v); break;
    case TWF_OAL: vm_set_oal(&w, v); break;
    case TWF_CUTLEN: vm_set_cut(&w, v); break;
    case TWF_SHLEN: vm_set_shlen(&w, v); break;
    case TWF_SHOULDER_DIA: vm_set_shdia(&w, v); break;
    case TWF_SHANK_DIA: vm_set_arbor(&w, v); break;
    case TWF_TIP_DIA: if (em_vm(ty)) rc = em_set_tip_dia(&w, v); else plain_set(&w, &t->tip_dia, v); break;   /* 23: TlToolFeedMillViewModel.TipDiameter */
    case TWF_TAPER_ANGLE: if (ty == 7) bo_set_taper(&w, v); else em_set_taper(&w, v); break;
    case TWF_TIP_ANGLE: plain_set(&w, &t->tip_angle, v); break;
    case TWF_CORNER_R: if (em_vm(ty)) em_set_cr(&w, v); else plain_set(&w, &t->corner_r, v); break;   /* 7 drill VM, 25 barrel VM */
    case TWF_CHAMFER: if (em_vm(ty)) em_set_chamfer(&w, v); else plain_set(&w, &t->chamfer_len, v); break;   /* 6 reamer VM */
    case TWF_SEC_CORNER_R: sl_set_sec_cr(&w, v); break;
    case TWF_SEC_CHAMFER: sl_set_sec_chamfer(&w, v); break;
    case TWF_SHOULDER_ANGLE: plain_set(&w, &t->shoulder_angle, v); break;
    case TWF_DRILL_DIA: plain_set(&w, &t->drill_dia, v); break;
    case TWF_DRILL_LEN: plain_set(&w, &t->drill_len, v); break;
    case TWF_THREAD_INFO:            /* ThreadInfo setter: 0 ignored; inch stores 1 / value */
        if (v != 0.0) th_set_pitch(&w, t->is_metric ? v : 1.0 / v);
        break;
    case TWF_PROFILE_R: plain_set(&w, &t->profile_r, v); break;
    case TWF_LOWER_R: plain_set(&w, &t->lower_r, v); break;
    case TWF_UPPER_R: plain_set(&w, &t->upper_r, v); break;
    case TWF_RELIEF: plain_set(&w, &t->relief_taper, v); break;
    case TWF_UPPER_TAPER_C: plain_set(&w, &t->upper_taper, 90.0 - v); break;   /* AngleComplimentConverter.ConvertBack */
    case TWF_LOWER_TAPER_C: plain_set(&w, &t->lower_taper, 90.0 - v); break;
    case TWF_SH_TAPER_ANGLE:         /* NonCuttingGeomViewModel.ShoulderTaperAngle setter */
        if (fabs(ncg_taper_angle(t) - v) > TOL) { vm_set_taper_len(&w, ncg_len_from_angle(t, v)); ncg_auto_adjust(&w); }
        break;
    case TWF_SH_TAPER_LEN: case TWF_NECK_LEN:   /* ShoulderTaperLength setter */
        if (fabs(t->taper_len - v) > TOL) { vm_set_taper_len(&w, v); ncg_auto_adjust(&w); }
        break;
    case TWF_NECK_DIA:               /* ReducedNeckDiameter setter */
        if (fabs(t->neck_dia - v) > TOL) { vm_set_neck(&w, v); ncg_auto_adjust(&w); }
        break;
    default: return 0;
    }
    if (rc < 0) return 0;            /* the setter threw: no SourceUpdated */
    const char *box = tw_box_name(ty, field);
    if (box && wired(ty, box)) page_validate(&w, box);
    return 1;
}

/* ===================================================================================================== corner / shank / combos */

static const char *const tabs4[] = {"No Corner", "Corner Chamfer", "Corner Radius", "Full Radius"};   /* EndmillViewXaml.CornerButton*_ToolTip */
static const char *const tabs_slot[] = {"No Corner", "Corner Chamfer", "Corner Radius"};             /* SlotMillViewXaml.CornerButton*_ToolTip */
static const char *const tabs2[] = {"No Corner", "Corner Radius"};       /* dove / boring tab images (EndmillViewXaml tooltips) */
static const char *const face_items[] = {"Chamfer", "Corner Radius", "Full Radius"};   /* FacemillView ctor: CornerTypeList 1,2,3 descriptions */
static const char *const taper_items[] = {"None", "Corner Radius", "Full Radius"};     /* TaperMillView ctor: CornerTypeList 0,2,3 */
static const char *const tap_items[] = {"Plug", "Bottoming"};            /* TapDrillView ctor: TapDrillTypes_Plug / _Bottoming */

/* labels of TYPE's corner control (tab tooltips or combo items); for taps (4/5) the Tip treatment combo */
int tw_corner_items(int type, const char *const **labels) {
    switch (type) {
    case 10: case 11: case 19: *labels = tabs4; return 4;
    case 14: *labels = tabs_slot; return 3;
    case 16: case 7: *labels = tabs2; return 2;
    case 13: *labels = face_items; return 3;
    case 17: *labels = taper_items; return 3;
    case 4: case 5: *labels = tap_items; return 2;
    }
    *labels = NULL; return 0;
}

/* the CornerType getters: EndmillPageViewModel :34-50, FacemillPageViewModel :25-43, TaperMillPageViewModel :29-46,
   DoveMillPageViewModel / BoringPageViewModel :23-38, SlotMillPageViewModel.LowerCornerType; taps: tw_tap_tip_index */
int tw_corner_index(const Tool *t) {
    switch (t->type) {
    case 10: case 11: case 19: case 14: return t->radius_type;
    case 13: switch (t->radius_type - 1) { case 0: return 0; case 1: return 1; case 2: return 2; default: return 0; }
    case 17: switch (t->radius_type) { case 0: return 0; case 2: return 1; case 3: return 2; default: return 0; }
    case 16: case 7: return t->radius_type == 2 ? 1 : 0;
    case 4: case 5: return tw_tap_tip_index(t);
    }
    return -1;
}

void tw_set_corner_index(Tool *t, TwState *st, int index) {
    W w = {t, st, 0};
    switch (t->type) {
    case 10: case 11: case 19:       /* EndmillPageViewModel.CornerType setter */
        if (index != t->radius_type) em_set_rtype(&w, index);
        break;
    case 14: slot_set_lower(&w, index); break;   /* LowerCornerType */
    case 13:                         /* FacemillPageViewModel.FaceMillSelectionChanged :52-67 */
        if (index >= 0 && index <= 2) em_set_rtype(&w, index + 1);
        break;
    case 17:                         /* TaperMillPageViewModel.TaperMillSelectionChanged :55-70 */
        if (index == 0) em_set_rtype(&w, 0); else if (index == 1) em_set_rtype(&w, 2); else if (index == 2) em_set_rtype(&w, 3);
        break;
    case 16: em_set_rtype(&w, index == 1 ? 2 : 0); break;   /* DoveMillPageViewModel.CornerType setter */
    case 7: bo_set_rtype(&w, index == 1 ? 2 : 0); break;    /* BoringPageViewModel.CornerType setter */
    case 4: case 5: tw_set_tap_tip_index(t, st, index); break;
    }
}

int tw_slot_upper_index(const Tool *t) { return t->sec_radius_type; }   /* SlotMillPageViewModel.UpperCornerType getter */
void tw_set_slot_upper_index(Tool *t, TwState *st, int index) { W w = {t, st, 0}; slot_set_upper(&w, index); }

/* SlotMillPageViewModel.AreCornersEqual :152-177 */
static int slot_corners_equal(const Tool *t) {
    if (t->radius_type != t->sec_radius_type) return 0;
    switch (t->radius_type - 1) {
    case 1: return fabs(t->corner_r - t->sec_corner_r) < TOL;
    case 0: return fabs(t->chamfer_len - t->sec_chamfer) < TOL;
    }
    return 1;
}
/* SlotMillPageViewModel.LockUpperAndLowerCorners setter :66-81 -> UpdateCornerObservers / UpdateCornerData :101-150.
   UpdateCornerData runs before the observers are registered, so st->lock_corners is set after it. */
void tw_set_lock_corners(Tool *t, TwState *st, int on) {
    on = on != 0;
    if (st->lock_corners == on) return;
    if (on) {
        W w = {t, st, 0};
        slot_set_lower(&w, t->sec_radius_type);
        em_set_chamfer(&w, t->sec_chamfer);
        em_set_cr(&w, t->sec_corner_r);
    }
    st->lock_corners = on;
}

/* NonCuttingGeometryView tab IsEnabled bindings: tab 0 always, 1 TaperedShankPossible, 2 ReducedNeckPossible */
int tw_shank_possible(const Tool *t, int style) {
    if (style == 1) return ncg_tapered_possible(t);
    if (style == 2) return ncg_neck_possible(t);
    return style == 0;
}
/* NonCuttingGeomViewModel.ShankStyleType setter :15-31: ShankType (whose observer runs InitializeValuesOnTypeChange) then
   InitializeValuesOnTypeChange again */
void tw_set_shank_style(Tool *t, TwState *st, int style) {
    if (t->shank_type == style) return;
    W w = {t, st, 0};
    vm_set_shank_type(&w, style);
    ncg_init_type(&w);
}

/* combo / tab driven IsEnabled: FacemillView.FaceMillSelectionChanged, TaperMillView.TaperMillSelectionChanged (both run on
   load when the OneWay SelectedIndex is first set), NonCuttingGeometryView tab IsEnabled */
int tw_box_enabled(const Tool *t, int field) {
    if (t->type == 13) {
        int i = tw_corner_index(t);
        if (field == TWF_TAPER_ANGLE) return i == 0 || i == 1;
        if (field == TWF_CORNER_R) return i != 0;
        if (field == TWF_DIA) return i == 0 || i == 1;
    } else if (t->type == 17) {
        int i = tw_corner_index(t);
        if (field == TWF_CORNER_R) return i != 0;
        if (field == TWF_DIA) return i != 2;
    }
    if (has_ncg(t->type)) {
        if (field == TWF_SH_TAPER_ANGLE || field == TWF_SH_TAPER_LEN) return ncg_tapered_possible(t);
        if (field == TWF_NECK_DIA || field == TWF_NECK_LEN) return ncg_neck_possible(t);
    }
    return 1;
}

/* TapDrillPageViewModel.LeftHanded setter :55-76 (the LeftHand SourceUpdated reaches ValidateEditFieldInput, no rule) */
void tw_set_left_hand(Tool *t, TwState *st, int on) {
    W w = {t, st, 0};
    if (!on) { vm_set_type(&w, 4); t->spindle_dir = 0; }
    else { vm_set_type(&w, 5); t->spindle_dir = 1; }
}
/* TapDrillPageViewModel.TipTreatment getter :78-92 */
int tw_tap_tip_index(const Tool *t) {
    if (fabs(t->tip_angle - 180.0) < TOL) return 1;
    if (fabs(t->tip_angle - 120.0) < TOL || fabs(t->tip_angle - 118.0) < TOL) return 0;
    return 1;
}
/* TapDrillPageViewModel.TapDrillSelectionChanged :103-115 (useOldTipAngle from the ctor :100) */
void tw_set_tap_tip_index(Tool *t, TwState *st, int index) {
    W w = {t, st, 0};
    int use_old = st && fabs(st->orig_tip_angle - 120.0) < TOL;
    if (index == 0) plain_set(&w, &t->tip_angle, use_old ? 120.0 : 118.0);
    else if (index == 1) plain_set(&w, &t->tip_angle, 180.0);
}

/* thread form combo: ThreadShapeList values in order (DB/Resources/ThreadShapeList.cs), DatabaseEnumStrings ThreadShapeList_* */
static const char *const thread_forms[] = {"Unified National", "Metric", "Acme", "Acme, truncated (Stub)",
                                           "American Buttress Pull", "American Buttress Push", "American National Pipe Thread"};
int tw_thread_form_items(const char *const **labels) { *labels = thread_forms; return 7; }
/* TlToolThreadingViewModel.ThreadShape setter */
void tw_set_thread_form(Tool *t, TwState *st, int shape) { W w = {t, st, 0}; if (t->thread_shape != shape) { t->thread_shape = shape; model_dirty(&w); } }
/* ThreadMillPageViewModel.ThreadInfoLabel / tapdrillview.baml ThreadPerTextBlock DataTrigger (ThreadPerInch_Text / Pitch_Text) */
const char *tw_thread_info_label(const Tool *t) { return t->is_metric ? "Pitch:" : "Threads per inch:"; }

/* ===================================================================================================== validity */

/* ToolValidityChecker.CheckToolValidity (DB/ValidityChecking/ToolValidityChecker.cs): ValidateTool((dynamic)mill) - the
   overload of the most derived entity class (DB/Services/ToolTypeFactory.cs maps types to classes). Messages: InvalidToolCode
   descriptions = InvalidToolStrings.resx (InvalidToolException ctor). A custom tool with an imported profile (IsCustom) goes
   through LoadCustomToolProfile instead (ToolProfileGenerator.cs:93-103): NOT DECODED here, Tool has no profile data. */
#define M_IllegalToolLength "Tool length must be greater than 0.0."
#define M_IllegalToolDiameter "Tool diameter must be greater than 0.0."
#define M_IllegalFluteShoulderLengths "Cutting length exceeds shoulder length or is less than or equal to 0.0."
#define M_IllegalFluteLength "Cutting length exceeds shoulder or overall length."
#define M_IllegalShoulderLength "Shoulder length exceeds tool length."
#define M_IllegalArborDiameter "Shank diameter must be greater than 0.0."
#define M_IllegalReducedNeckRadius "The reduced neck radius must be greater than 0.0."
#define M_ReducedNeckLargerThanShoulderShank "The reduced neck diameter cannot be larger than the shoulder diameter or the shank diameter."
#define M_NoNeckLength "The taper length must be larger to support a neck length."
#define M_ToolShorterThanSumOfComponents "The total tool length (Overall) is less than the total of all of its components, and illegal condition."
#define M_IllegalCornerRadius "Corner radius must be greater than 0.0."
#define M_CornerRadiusOrFluteLengthError "Corner radius and/or cutting length error."
#define M_ArborDiameterTooLarge "Shank diameter too large."
#define M_ShoulderDiameterCantBeZero "Shoulder diameter cannot be zero."
#define M_LowerRadiusLargerThanProfileRadius "The corner radius is too large for the current profile radius."
#define M_ShoulderDiameterLargerThanToolDiameter "The shoulder diameter cannot be larger than the cutting diameter."
#define M_ProfileRadiusTooSmall "The profile radius is too small for the current tool dimensions."
#define M_ProfileRadiusTooLarge "The profile radius is too large for the current tool dimensions."
#define M_ReliefAngleNonZeroWithInvalidDiameters "Relief angle must be zero if the shoulder diameter is not smaller than the cutting diameter."
#define M_InvalidReliefTaperAngle "The relief taper angle must fall between 0.0 and 90.0."
#define M_TipDiameterExceedsToolDiameter "Tip diameter exceeds tool diameter."
#define M_InvalidLowerTaperAngle "The lower taper angle must fall between 0.0 and the upper taper angle."
#define M_InvalidUpperTaperAngle "The upper taper angle must fall between the lower taper angle and 90.0."
#define M_ArborRadiusExceedsToolRadius "Shank diameter too large for tool diameter."
#define M_ToolDiameterEqualsArborDiameter "Tool diameter can not equal shank diameter."
#define M_TaperAngleOrFluteLengthError "Taper angle and/or cutting length error."
#define M_TipAngleTooSteep "Tip angle too steep for tool diameter."
#define M_ShoulderAngleTooSteep "Shoulder angle too steep for tool diameter."
#define M_ToolLengthTooShortForDiameter "Tool length too short for tool diameter."
#define M_ShoulderLengthTooShortForDiameter "Shoulder length too short for tool diameter."
#define M_ChamferDistanceTooLargeForDiameter "Chamfer distance too large for cutting diameter."
#define M_CornerRadiusTooLargeForRadius "Corner radius too large for cutting diameter."
#define M_ToolLengthCornerRadError "Corner radius too large for tool length."
#define M_ShoulderLengthCornerRadError "Corner radius too large for shoulder length."
#define M_ShoulderDiameterLargerThanArbor "The shoulder diameter is greater than the shank diameter, an illegal condition."
#define M_ToolLengthTooShort "Tool length too short."
#define M_TipToolDiameterIntersectionError "Tip angle/tool diameter don't intersect."
#define M_ThreadPitchTooLargeForDiameter "Thread pitch too large for tool diameter."

static const char *v_mill(const Tool *m) {                     /* ValidateTool(TlToolMill) :36-93 */
    if (IsLessThanOrEqualTo(m->oal, 0.0)) return M_IllegalToolLength;
    if (IsLessThanOrEqualTo(m->dia, 0.0)) return M_IllegalToolDiameter;
    if (IsLessThanOrEqualTo(m->flute_len, 0.0)) return M_IllegalFluteShoulderLengths;
    if (IsGreaterThan(m->flute_len, m->oal)) return M_IllegalFluteLength;
    if (IsGreaterThan(m->shoulder_len, m->oal)) return M_IllegalShoulderLength;
    if (IsGreaterThan(m->dia, m->arbor_dia) && IsGreaterThan(m->shoulder_len, m->oal)) return M_IllegalShoulderLength;
    if (IsGreaterThan(m->flute_len, m->shoulder_len)) return M_IllegalFluteShoulderLengths;
    if (IsLessThanOrEqualTo(m->arbor_dia, 0.0)) return M_IllegalArborDiameter;
    if (m->shank_type == 2) {
        if (IsLessThanOrEqualTo(m->neck_dia, 0.0)) return M_IllegalReducedNeckRadius;
        double num = m->shoulder_dia - m->neck_dia, num2 = m->arbor_dia - m->neck_dia;
        if (IsLessThan(num, 0.0) || IsLessThan(num2, 0.0)) return M_ReducedNeckLargerThanShoulderShank;
        double num3 = m->shoulder_len + num, num4 = m->shoulder_len + m->taper_len;
        if (IsLessThan(num4 - num2 - num3, 0.0)) return M_NoNeckLength;
        if (IsLessThanOrEqualTo(m->oal - num4, 0.0)) return M_ToolShorterThanSumOfComponents;
    }
    return NULL;
}
static const char *v_endmill(const Tool *m) {                  /* ValidateEndmill :325-351 */
    if (IsEqualTo(m->shoulder_dia, 0.0)) return M_ShoulderDiameterCantBeZero;
    if (IsGreaterThan(m->chamfer_len, m->dia / 2.0)) return M_ChamferDistanceTooLargeForDiameter;
    if (IsLessThan(m->corner_r, 0.0)) return M_IllegalCornerRadius;
    if (IsGreaterThan(m->corner_r, m->dia / 2.0)) return M_CornerRadiusTooLargeForRadius;
    if (IsGreaterThan(m->corner_r, m->oal)) return M_ToolLengthCornerRadError;
    if (IsGreaterThan(m->corner_r, m->shoulder_len)) return M_ShoulderLengthCornerRadError;
    return NULL;
}
static const char *v_roundover(const Tool *m) {                /* ValidateRoundoverTool :353-363 */
    if (IsLessThanOrEqualTo(m->corner_r, 0.0)) return M_IllegalCornerRadius;
    if (IsGreaterThanOrEqualTo(m->corner_r, m->shoulder_len)) return M_ShoulderLengthCornerRadError;
    return NULL;
}
static const char *v_dove(const Tool *b) {                     /* ValidateDoveTailTool :365-393 */
    double num = tan((90.0 - b->taper_angle) * TW_PI / 180.0);
    double value = b->dia / 2.0 - b->flute_len / num;
    if (IsLessThan(b->corner_r, 0.0)) return M_IllegalCornerRadius;
    if (IsGreaterThan(b->corner_r, b->flute_len)) return M_CornerRadiusOrFluteLengthError;
    if (IsGreaterThan(b->corner_r, b->dia / 2.0)) return M_CornerRadiusTooLargeForRadius;
    if (IsGreaterThan(b->arbor_dia, b->dia)) return M_ArborRadiusExceedsToolRadius;
    if (IsEqualTo(b->arbor_dia, b->dia)) return M_ToolDiameterEqualsArborDiameter;
    if (IsLessThanOrEqualTo(value, 0.0) && IsEqualTo(b->corner_r, 0.0)) return M_TaperAngleOrFluteLengthError;
    return NULL;
}
static const char *v_lollipop(const Tool *m) {                 /* ValidateLollipopTool :395-431 */
    int flag = m->shank_type == 0, flag2 = IsEqualTo(m->taper_len, 0.0), flag3 = IsEqualTo(m->shoulder_dia, m->arbor_dia);
    int flag4 = !flag && !flag2 && !flag3;
    if (IsEqualTo(m->shoulder_dia, 0.0)) return M_ShoulderDiameterCantBeZero;
    if (!flag4 && IsGreaterThan(m->arbor_dia, m->dia)) return M_ArborRadiusExceedsToolRadius;
    if (flag4 && IsEqualTo(m->shoulder_dia, m->arbor_dia)) return M_ShoulderDiameterLargerThanArbor;
    if (IsLessThan(m->oal, m->shoulder_len + m->taper_len)) return M_ToolShorterThanSumOfComponents;
    if (IsGreaterThan(m->shoulder_dia, m->arbor_dia)) return M_ShoulderDiameterLargerThanArbor;
    if (IsGreaterThan(m->shoulder_dia, m->dia)) return M_ShoulderDiameterLargerThanToolDiameter;
    double val = sqrt(m->dia / 2.0 * (m->dia / 2.0) - m->shoulder_dia / 2.0 * (m->shoulder_dia / 2.0)) + m->dia / 2.0;
    if (IsLessThan(m->shoulder_len, val)) return M_ShoulderLengthTooShortForDiameter;
    return NULL;
}
static const char *v_face(const Tool *m) {                     /* ValidateFaceMillTool :441-482 */
    double num = IsGreaterThanOrEqualTo(m->taper_angle, 90.0) ? 0.0 : m->taper_angle;
    double num2 = tan((90.0 - num) * TW_PI / 180.0);
    double value = (m->tip_dia / 2.0 - m->dia / 2.0) * num2;
    if (IsEqualTo(num, 0.0)) value = 0.0;
    if (IsLessThan(m->oal, m->dia / 8.0)) return M_ToolLengthTooShort;
    if (IsGreaterThan(m->dia, m->tip_dia)) return M_TipDiameterExceedsToolDiameter;
    if (IsLessThan(value, 0.0) || IsGreaterThan(value, m->shoulder_len)) return M_TipToolDiameterIntersectionError;
    if (IsLessThan(m->corner_r, 0.0)) return M_IllegalCornerRadius;
    if (IsGreaterThan(m->corner_r, m->flute_len)) return M_CornerRadiusOrFluteLengthError;
    if (IsEqualTo(m->dia, m->tip_dia) && !IsEqualTo(num, 0.0)) return M_TipToolDiameterIntersectionError;
    if (IsEqualTo(m->dia, m->tip_dia) && IsEqualTo(num, 0.0) && IsGreaterThan(m->corner_r, 0.0)) return M_TipToolDiameterIntersectionError;
    if (IsEqualTo(num, 0.0) && IsGreaterThan(m->corner_r, m->shoulder_len)) return M_TipToolDiameterIntersectionError;
    return NULL;
}
static const char *v_engrave_chamfer(const Tool *m) {          /* ValidateEngraveOrChamferMillTool :484-504 */
    double num = tan((90.0 - m->taper_angle) * TW_PI / 180.0);
    double value = (m->dia / 2.0 - m->tip_dia / 2.0) * num;
    if (IsLessThan(m->oal, m->dia / 8.0)) return M_ToolLengthTooShort;
    if (IsGreaterThanOrEqualTo(m->tip_dia, m->dia)) return M_TipDiameterExceedsToolDiameter;
    if (IsLessThan(value, 0.0) || IsGreaterThan(value, m->shoulder_len)) return M_TipToolDiameterIntersectionError;
    if (IsLessThan(m->corner_r, 0.0)) return M_IllegalCornerRadius;
    return NULL;
}
static const char *v_slot(const Tool *m) {                     /* ValidateTool(TlToolSlotMill) :130-151 */
    double num = IsGreaterThan(m->corner_r, 0.0) ? m->corner_r : m->chamfer_len;
    double num2 = IsGreaterThan(m->corner_r, 0.0) ? m->sec_corner_r : m->sec_chamfer;
    if (IsLessThan(m->corner_r, 0.0) || IsLessThan(m->sec_corner_r, 0.0)) return M_IllegalCornerRadius;
    if (IsGreaterThan(num, m->flute_len / 2.0) || IsGreaterThan(num2, m->flute_len / 2.0)) return M_CornerRadiusOrFluteLengthError;
    if (IsLessThan(m->flute_len, num + num2)) return M_CornerRadiusOrFluteLengthError;
    if (IsGreaterThan(m->arbor_dia / 2.0, m->dia / 2.0 - num2)) return M_ArborDiameterTooLarge;
    return NULL;
}
static const char *v_barrel(const Tool *m) {                   /* ValidateTool(TlToolBarrelMill) :153-202 */
    double num = m->dia / 2.0, num2 = m->shoulder_dia / 2.0, cr = m->corner_r, pr = m->profile_r, cut = m->flute_len;
    if (IsEqualTo(num2, 0.0)) return M_ShoulderDiameterCantBeZero;
    if (IsGreaterThanOrEqualTo(cr, pr)) return M_LowerRadiusLargerThanProfileRadius;
    if (IsLessThan(num, num2)) return M_ShoulderDiameterLargerThanToolDiameter;
    if (IsGreaterThan(cut, pr * 2.0)) return M_ProfileRadiusTooSmall;
    if (IsGreaterThan(num - num2, pr)) return M_ProfileRadiusTooSmall;
    double num3 = num - pr;
    double num4 = cut - sqrt(pow(pr, 2.0) - pow(pr + num2 - num, 2.0));
    if (IsGreaterThan(num4, pr)) return M_ProfileRadiusTooSmall;
    if (IsLessThan(num4, 0.0)) return M_ProfileRadiusTooLarge;
    if (IsLessThan(num4, cr)) return M_LowerRadiusLargerThanProfileRadius;
    double x = pr - cr, x2 = num4 - cr;
    double num5 = sqrt(pow(x, 2.0) - pow(x2, 2.0));
    if (IsLessThan(num3 + num5, 0.0) && IsGreaterThan(cr, 0.0)) return M_ProfileRadiusTooSmall;
    return NULL;
}
static const char *v_feed(const Tool *m) {                     /* ValidateTool(TlToolFeedMill) :204-235 */
    if (IsEqualTo(m->shoulder_dia, 0.0)) return M_ShoulderDiameterCantBeZero;
    if (IsGreaterThan(m->relief_taper, 0.0) && IsLessThanOrEqualTo(m->dia, m->shoulder_dia)) return M_ReliefAngleNonZeroWithInvalidDiameters;
    if (IsLessThan(m->relief_taper, 0.0) || IsGreaterThan(m->relief_taper, 90.0)) return M_InvalidReliefTaperAngle;
    if (IsLessThanOrEqualTo(m->dia, m->tip_dia)) return M_TipDiameterExceedsToolDiameter;
    if (IsGreaterThan(m->lower_taper, 90.0)) return M_InvalidLowerTaperAngle;
    if (IsGreaterThan(m->upper_taper, 90.0) || IsLessThan(m->upper_taper, 0.0)) return M_InvalidUpperTaperAngle;
    if (IsLessThan(m->lower_taper, m->upper_taper)) return M_InvalidLowerTaperAngle;
    return NULL;
}
static const char *v_boring(const Tool *b) {                   /* ValidateTool(TlToolBoring) :242-263 (uses TipAngle) */
    double num = tan((90.0 - b->tip_angle) * TW_PI / 180.0);
    double value = b->dia / 2.0 - b->flute_len / num;
    if (IsLessThan(b->corner_r, 0.0)) return M_IllegalCornerRadius;
    if (IsGreaterThan(b->arbor_dia / 2.0, b->dia / 2.0)) return M_ArborRadiusExceedsToolRadius;
    if (IsEqualTo(b->arbor_dia, b->dia)) return M_ToolDiameterEqualsArborDiameter;
    if (IsLessThanOrEqualTo(value, 0.0)) return M_TaperAngleOrFluteLengthError;
    return NULL;
}
/* ValidateCounterSinkTool :506-532 / ValidateTap :545-571 (the tap's "is R zero" test is < 5E-06) */
static const char *v_sink_or_tap(const Tool *s, int tap) {
    if (tap ? fabs(s->corner_r - 0.0) < 5E-06 : IsEqualTo(s->corner_r, 0.0)) {
        double num = s->dia / 2.0, a = (90.0 - s->tip_angle / 2.0) * TW_PI / 180.0, value = num * tan(a);
        if (IsLessThan(value, 0.0) || IsGreaterThan(value, s->shoulder_len)) return M_TipToolDiameterIntersectionError;
        return NULL;
    }
    double value2 = (90.0 - s->tip_angle / 2.0) * TW_PI / 180.0;
    if (IsLessThanOrEqualTo(value2, 0.0)) return M_IllegalCornerRadius;
    if (IsLessThan(s->corner_r, 0.0)) return M_IllegalCornerRadius;
    if (IsGreaterThan(s->corner_r, s->dia / 2.0)) return M_CornerRadiusTooLargeForRadius;
    return NULL;
}
static const char *v_drill(const Tool *d) {                    /* ValidateDrillTool :534-543 */
    double num = d->dia / 2.0, a = (90.0 - d->tip_angle / 2.0) * TW_PI / 180.0, value = num * tan(a);
    if (IsLessThan(value, 0.0) || IsGreaterThan(value, d->shoulder_len)) return M_TipToolDiameterIntersectionError;
    return NULL;
}
static const char *v_center(const Tool *d) {                   /* ValidateTool(TlToolCenterDrill) :278-293 */
    double a = (90.0 - d->tip_angle / 2.0) * TW_PI / 180.0, a2 = (90.0 - d->shoulder_angle / 2.0) * TW_PI / 180.0;
    double value = d->drill_dia / 2.0 * tan(a);
    double value2 = (d->dia - d->drill_dia) / 2.0 * tan(a2) + d->drill_len;
    if (IsGreaterThanOrEqualTo(value, d->drill_len)) return M_TipAngleTooSteep;
    if (IsGreaterThan(value2, d->shoulder_len)) return M_ShoulderAngleTooSteep;
    return NULL;
}
static const char *v_reamer(const Tool *r) {                   /* ValidateTool(TlToolReamer) :295-310 */
    if (IsLessThan(r->oal, r->dia / 8.0)) return M_ToolLengthTooShortForDiameter;
    if (!IsEqualTo(r->dia, r->arbor_dia) && IsLessThan(r->shoulder_len, r->dia / 8.0)) return M_ShoulderLengthTooShortForDiameter;
    if (IsLessThan(r->dia, r->chamfer_len * 2.0)) return M_ChamferDistanceTooLargeForDiameter;
    return NULL;
}
static const char *v_thread_mill(const Tool *t) {              /* ValidateThreadMill :573-607 */
    if (IsEqualTo(t->shoulder_dia, 0.0)) return M_ShoulderDiameterCantBeZero;
    double num = t->dia / 2.0, cut = t->flute_len, p = t->pitch, value = num;
    switch (t->thread_shape) {
    case 0: case 1: value = num - 0.866 * p; break;
    case 2: value = num - 0.5 * p; break;
    case 3: value = num - 0.3 * p; break;
    case 4: case 5: value = num + 0.0826 * p - 0.89064 * p; break;
    case 6: value = num - cut * (1.0 / 32.0) + 0.8657 * p; break;
    }
    if (IsLessThanOrEqualTo(value, 0.0)) return M_ThreadPitchTooLargeForDiameter;
    return NULL;
}

const char *tw_validate(const Tool *t) {
    const char *e = v_mill(t);
    if (e) return e;
    switch (t->type) {
    case 10: case 11: case 19: return v_endmill(t);          /* ValidateTool(TlToolEndmill) :95-128 */
    case 15: return v_roundover(t);
    case 16: return v_dove(t);
    case 18: return v_lollipop(t);
    case 17: return IsLessThan(t->corner_r, 0.0) ? M_IllegalCornerRadius : NULL;   /* ValidateTaperMillTool :433-439 */
    case 13: return v_face(t);
    case 12: case 21: return v_engrave_chamfer(t);
    case 14: return v_slot(t);
    case 25: return v_barrel(t);
    case 23: return v_feed(t);
    case 8: return NULL;                                     /* ValidateTool(TlToolCounterBore) :237-240 */
    case 7: return v_boring(t);
    case 1: return v_center(t);
    case 9: return v_sink_or_tap(t, 0);                       /* ValidateTool(TlToolDrill) :265-276 */
    case 2: case 3: case 22: return v_drill(t);
    case 6: return v_reamer(t);
    case 24: return v_thread_mill(t);                         /* ValidateTool(TlToolThreading) :312-323 */
    case 4: case 5: return v_sink_or_tap(t, 1);
    }
    return NULL;                                             /* 0 custom (TlToolMill): common checks only */
}

/* ===================================================================================================== naming */

/* CreateAutoGeneratedName:
   - TlToolViewModel.cs:821-828: string.Format(InvariantCulture, "{0} {1}" [TlToolViewModelStrings.Tool_Name], OverallDiameter,
     EnumExtensions.ToDescription(MCToolType, ", "))
   - TlToolEndmillViewModel.cs:257-264: type 17: ArborDiameter + " " + description
   - TlToolDrillViewModel.cs:171-178: (not 7) StandardSizeNamingSeed + description + " - " + OverallDiameter
   - TlToolThreadingViewModel.cs:163-166: StandardSizeNamingSeed + " " + description + " - " + OverallDiameter
   ToDescription (LOC/EnumExtensions.cs:13-24,92-99,149-161): ToolTypeList is not [Flags], so the separator is unused and
   the text is the LocalizedDescription attribute (an undefined value prints as its number). The "+" concatenations format
   the double with the current culture: NOT DECODED, taken as '.' decimals like InvariantCulture. */
static void auto_name(const Tool *t, const char *seed, char *out, int n) {
    char num[64], d[32];
    const char *desc = tw_type_description(t->type);
    if (!desc) { snprintf(d, sizeof d, "%d", t->type); desc = d; }
    if (!seed) seed = "";
    int ty = t->type;
    if (thread_vm(ty)) { net_g15(t->dia, num, sizeof num); snprintf(out, n, "%s %s - %s", seed, desc, num); }
    else if (drill_vm(ty) && ty != 7) { net_g15(t->dia, num, sizeof num); snprintf(out, n, "%s%s - %s", seed, desc, num); }
    else if (ty == 17) { net_g15(t->arbor_dia, num, sizeof num); snprintf(out, n, "%s %s", num, desc); }
    else { net_g15(t->dia, num, sizeof num); snprintf(out, n, "%s %s", num, desc); }
}
/* IsAutoGeneratedName: TlToolDrillViewModel / TlToolThreadingViewModel return true; TlToolViewModel / TlToolEndmillViewModel
   compare with CreateAutoGeneratedName() */
static int is_auto_name(const Tool *t, const char *seed, const char *name) {
    if (drill_vm(t->type) || thread_vm(t->type)) return 1;
    char a[256]; auto_name(t, seed, a, sizeof a);
    return !strcmp(a, name);
}

/* TlAssemblyItemViewModel.AutoGenerateName (TlAssemblyItemViewModel.cs:467-473): if !NameManuallyChanged &&
   GeneralOptions.AutoNaming (default true, DB/Options/GeneralOptions.cs:129; the option is not ported) */
void tw_autoname(Tool *t, const TwState *st) {
    if (st && st->name_manual) return;
    auto_name(t, st ? st->seed : "", t->name, (int)sizeof t->name);   /* truncated to Tool.name */
}
/* TlAssemblyItemViewModel.Name setter (:168-188): a name that is not the auto one marks NameManuallyChanged */
void tw_set_name(Tool *t, TwState *st, const char *name) {
    if (!is_auto_name(t, st->seed, name)) st->name_manual = 1;
    size_t k = strlen(name); if (k >= sizeof t->name) k = sizeof t->name - 1;
    memcpy(t->name, name, k); t->name[k] = 0;
}

/* ===================================================================================================== properties page: feed / speed */

/* Sfm / Fpt getters: Math.Round(Calculate*(), RoundingConstant) with RoundingConstant = IsMetric ? 6 : 5 (TlToolViewModel.cs:710-763) */
double tw_sfm(const Tool *t) { return net_round(calc_sfm(t), t->is_metric ? 6 : 5); }
double tw_fpt(const Tool *t) { return net_round(calc_fpt(t), t->is_metric ? 6 : 5); }

/* Sfm setter (:710-729) */
void tw_set_sfm(Tool *t, double sfm) {
    if (!vchg(calc_sfm(t), sfm)) return;
    double fpt = calc_fpt(t);
    int ok, r = calc_spindle(t, sfm, &ok);
    if (!ok) return;
    set_rpm_value(t, r);
    t->feed = calc_feed(t, abs(t->rpm), fpt);
}
/* Fpt setter (:731-747) */
void tw_set_fpt(Tool *t, double fpt) {
    if (!vchg(calc_fpt(t), fpt)) return;
    t->feed = calc_feed(t, abs(t->rpm), fpt);
}
/* FeedRate setter (:294-319): taps re-derive the spindle speed (Convert.ToInt32(feed / CalculateFpt())) */
void tw_set_feed(Tool *t, double feed) {
    if (!vchg(t->feed, feed)) return;
    t->feed = feed;
    if (tap_vm(t->type)) { int ok, r = to_int32(feed / calc_fpt(t), &ok); if (ok) set_rpm_value(t, r); }
}
/* SpindleSpeed setter (:355-384): taps keep FPT (feed = rpm * pitch) */
void tw_set_rpm(Tool *t, int rpm) {
    if (abs(t->rpm) == rpm) return;
    if (tap_vm(t->type)) { double fpt = calc_fpt(t); set_rpm_value(t, rpm); t->feed = calc_feed(t, abs(t->rpm), fpt); }
    else set_rpm_value(t, rpm);
}
/* FluteCount setter (:412-430): keeps FPT */
void tw_set_flutes(Tool *t, int flutes) {
    if (t->flutes == flutes) return;
    double fpt = calc_fpt(t);
    t->flutes = flutes;
    t->feed = calc_feed(t, abs(t->rpm), fpt);
}
/* the speed part of the OverallDiameter setter (:394-409) for a diameter already changed from OLD_DIA */
void tw_set_dia_speeds(Tool *t, double old_dia) {
    double nd = t->dia; t->dia = old_dia;
    double sfm = calc_sfm(t), fpt = calc_fpt(t);
    t->dia = nd;
    if (t->type != 13) {
        int ok, r = calc_spindle(t, sfm, &ok);
        if (!ok) return;
        t->rpm = abs(r);
        t->feed = calc_feed(t, t->rpm, fpt);
    }
}

/* ===================================================================================================== numbers / setup */

/* LengthOffsetNum / DiameterOffsetNum setters (TlToolViewModel.cs:12-31, 55-74): changed and >= 0 */
static void set_off(int *o, int v) { if (*o != v && v >= 0) *o = v; }
/* TlToolViewModel.ToolNumber setter (:601-622) */
static int set_tool_number(Tool *t, int num) {
    if (num >= 0 && t->num != num) { set_off(&t->len_off, num); set_off(&t->dia_off, num); t->num = num; return 1; }
    return 0;
}
/* ToolNumber on the Properties page: ToolPropertyPageViewModel's ToolNumber observer -> RaisePropertyChanged
   (ToolPropertyPageViewModel.cs:66-96): LengthOffsetNum = ToolNumber + addToLoff, DiameterOffsetNum = ToolNumber + addToDoff */
void tw_set_tool_number(Tool *t, const TwState *st, int num) {
    if (set_tool_number(t, num)) { set_off(&t->len_off, t->num + st->add_loff); set_off(&t->dia_off, t->num + st->add_doff); }
}

/* page view model constructors' state: arbor/chamfer flags false (Endmill/Spot/BradPoint/Engrave/Reamer ctors), TapDrill's
   useOldTipAngle (:100), SlotMill's LockUpperAndLowerCorners = AreCornersEqual() (:89); TlTool*ViewModel's
   StandardSizeNamingSeed = "" */
static void page_ctor(Tool *t, TwState *st) {
    st->arbor_modified = 0; st->chamfer_modified = 0; st->lock_corners = 0; st->seed[0] = 0;
    st->orig_tip_angle = t->tip_angle;
    if (t->type == 14) tw_set_lock_corners(t, st, slot_corners_equal(t));
}

/* leaving page 1 (ToolWizardViewModel.PreviewSelectedPageChanging): GetDefaultTool(type, isMetric) -> SetUpViewModelWrapper
   (initializeOffsets: true) -> new page VM + ToolPropertyPageViewModel (creating: AutoGenerateName). The C# only replaces
   the pages when the page class or MCToolType differs from the current page's tool (ToolWizardViewModel.cs:136): the
   caller decides that. ST's add_loff / add_doff are kept. The old T's holder outline is not freed here. */
void tw_new_type(Tool *t, TwState *st, int type, int num, int metric) {
    tool_new(t, type, 0, 0, metric);            /* DefaultDatabaseManager.GetDefaultTool: defaults rows have ToolNumber 0 */
    set_tool_number(t, num);                    /* SetUpViewModelWrapper: ToolNumber = toolNumber */
    set_off(&t->len_off, num + st->add_loff);   /* initializeOffsets */
    set_off(&t->dia_off, num + st->add_doff);
    st->creating = 1; st->name_manual = 0; st->seed[0] = 0;
    if (t->name[0] && !is_auto_name(t, "", t->name)) st->name_manual = 1;   /* Name = Name */
    page_ctor(t, st);
    tw_autoname(t, st);                         /* ToolPropertyPageViewModel ctor, isCreating */
}

/* Edit (ToolWizardViewModel.InitializePageViewModels(EditToolWizardParameters)): IsCreating false, ToolNumber = the tool's own
   (no change), offsets not re-derived (initializeOffsets false), Name = Name, page VM ctor state, no auto name */
void tw_init_edit(Tool *t, TwState *st, int add_loff, int add_doff) {
    st->creating = 0; st->add_loff = add_loff; st->add_doff = add_doff; st->name_manual = 0; st->seed[0] = 0;
    if (t->name[0] && !is_auto_name(t, "", t->name)) st->name_manual = 1;
    page_ctor(t, st);
}

/* GeometryPageViewModel.BulkUpdateProperties -> <page>.BulkUpdatePropertiesInternal for a standard-size row (the XmlElement's
   attributes). The "overwrites tool geometry" warning when editing (BulkUpdateProperties :183) is the caller's. */
void tw_std_size(Tool *t, TwState *st, const TwStdRow *r) {
    W w = {t, st, 0};
    int mf = t->is_metric ? 5 : 6;
    switch (t->type) {
    case 2: case 3:                  /* SpotDrillPageViewModel :62-105 */
        if (r->has & TWS_DIA) { vm_set_dia(&w, r->dia); vm_set_arbor(&w, r->dia); }
        else if (r->has & TWS_DRILL_DIA) { vm_set_dia(&w, r->drill_dia); vm_set_arbor(&w, r->drill_dia); }
        if (r->has & TWS_OAL) vm_set_oal(&w, r->oal);
        else if (r->has & TWS_DRILL_LEN) { vm_set_cut(&w, r->drill_len); vm_set_shlen(&w, r->drill_len); }
        if (r->has & TWS_FLUTE_LEN) { vm_set_cut(&w, r->flute_len); vm_set_shlen(&w, r->flute_len); }
        if (r->name) { snprintf(st->seed, sizeof st->seed, "%s ", r->name); tw_autoname(t, st); }
        else if (r->size) { snprintf(st->seed, sizeof st->seed, "%s ", r->size); tw_autoname(t, st); }
        break;
    case 22:                         /* BradPointDrillPageViewModel :60-85 */
        if (r->has & TWS_DIA) { vm_set_dia(&w, r->dia); vm_set_arbor(&w, r->dia); }
        if (r->has & TWS_OAL) vm_set_oal(&w, r->oal);
        if (r->has & TWS_FLUTE_LEN) { vm_set_cut(&w, r->flute_len); vm_set_shlen(&w, r->flute_len); }
        if (r->name) { snprintf(st->seed, sizeof st->seed, "%s ", r->name); tw_autoname(t, st); }
        break;
    case 1:                          /* CenterDrillPageViewModel :55-86 */
        if (r->has & TWS_OAL) vm_set_oal(&w, r->oal);
        if (r->has & TWS_SHANK_DIA) { vm_set_arbor(&w, r->shank_dia); vm_set_dia(&w, r->shank_dia); }
        if (r->has & TWS_DRILL_DIA) plain_set(&w, &t->drill_dia, r->drill_dia);
        if (r->has & TWS_DRILL_LEN) plain_set(&w, &t->drill_len, r->drill_len);
        if (r->size) { snprintf(st->seed, sizeof st->seed, "%s ", r->size); tw_autoname(t, st); }
        vm_set_cut(&w, net_round(center_calc(t), mf));
        vm_set_shlen(&w, t->flute_len + 0.005);
        break;
    case 4: case 5:                  /* TapDrillPageViewModel :127-156 */
        if (r->has & TWS_OAL) vm_set_oal(&w, r->oal);
        if (r->has & TWS_DIA) { vm_set_dia(&w, r->dia); vm_set_arbor(&w, r->dia); }
        if (r->has & TWS_FLUTE_LEN) { vm_set_cut(&w, r->flute_len); vm_set_shlen(&w, r->flute_len); }
        if (r->has & TWS_THREAD_INFO) { if (r->thread_info != 0.0) th_set_pitch(&w, t->is_metric ? r->thread_info : 1.0 / r->thread_info); }
        if (r->name) { snprintf(st->seed, sizeof st->seed, "%s", r->name); tw_autoname(t, st); }
        break;
    }
}
