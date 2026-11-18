#include "acsettings.h"
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFontMetrics>
#include <cmath>
#include <cstdlib>
#include "core_ui/core_ui.h"

/* dialog units -> pixels as Windows' MapDialogRect does for a DS_SETFONT dialog: x * the font's average character
   width / 4, y * its height / 8 (average width = the width of "A..Za..z" / 52, rounded). With the template's MS Shell
   Dlg 8 at 96 dpi that is 6 x 13 (mcamui.c BS_DX 1.5 / BS_DY 1.625); here the dialog's own (Qt) font is measured so
   the decoded positions keep their texts inside them */
static double g_bx = 6, g_by = 13;
static QRect du(int x, int y, int w, int h) {
    return QRect((int)lround(x * g_bx / 4), (int)lround(y * g_by / 8), (int)lround(w * g_bx / 4), (int)lround(h * g_by / 8)); }

/* the template of dialog 779 (exports/opmanres_dialogs.tsv): 167 x 202 DLU, "AutoCursor Settings"; the checkboxes
   with their control ids and the AutoCursor mode they gate (the fast_data index map of formats/ui.md: decomp/
   mastercam_acinit.c / mastercam_acok.c) */
static const struct { int id, k, x, y, w, h; const char *t; } CB[] = {
    { 333, ACS_ORIGIN, 7, 7, 55, 12, "Origin" }, { 334, ACS_CENTER, 7, 21, 55, 12, "Center" }, { 336, ACS_END, 7, 35, 55, 12, "Endpoint" },
    { 337, ACS_INT, 7, 49, 55, 12, "Intersect" }, { 339, ACS_MID, 7, 63, 55, 12, "Midpoint" }, { 343, ACS_QUAD, 7, 77, 55, 12, "Quadrant" },
    { 341, ACS_POINT, 7, 91, 55, 12, "Point" }, { 352, ACS_ANG, 72, 7, 42, 12, "Angular" }, { 345, ACS_TAN, 72, 21, 75, 12, "Tangent" },
    { 347, ACS_PERP, 72, 35, 75, 12, "Perpendicular" }, { 350, ACS_NEAR, 72, 49, 75, 12, "Nearest" },
    { 359, ACS_HV, 72, 63, 80, 12, "Horizontal / Vertical" }, { 360, ACS_TMID, 72, 77, 80, 12, "Temporary Midpoints" } };

AcSettingsDialog::AcSettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("AutoCursor Settings");
    { QFontMetrics fm(font()); g_bx = (fm.horizontalAdvance("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") / 26 + 1) / 2; g_by = fm.height(); }
    setFixedSize(du(0, 0, 167, 202).size());
    for (const auto &c : CB) {
        auto *b = new QCheckBox(c.t, this); b->setGeometry(du(c.x, c.y, c.w, c.h)); b->setChecked(ac_snap[c.k] != 0);
        cb_[c.k] = b;
    }
    ang_ = new QLineEdit(QString::asprintf("%g", ac_ang_step), this); ang_->setGeometry(du(120, 7, 40, 12));   /* 289: the Angular step (degrees) */
    /* 537 "Enable All" / 539 "Disable All" (tooltips "Turn on all settings" / "Turn off all settings"): every ported mode
       (all of the checkboxes above - mcamui) */
    auto *en = new QPushButton("Enable All", this); en->setGeometry(du(28, 110, 50, 14)); en->setToolTip("Turn on all settings");
    auto *dis = new QPushButton("Disable All", this); dis->setGeometry(du(88, 110, 50, 14)); dis->setToolTip("Turn off all settings");
    connect(en, &QPushButton::clicked, this, [this] { for (auto *b : cb_) if (b) b->setChecked(true); });
    connect(dis, &QPushButton::clicked, this, [this] { for (auto *b : cb_) if (b) b->setChecked(false); });
    /* 357 "Default to Fast Point mode" / 362 "Enable power keys": not ported (greyed) */
    auto *fp = new QCheckBox("Default to Fast Point mode", this); fp->setGeometry(du(7, 134, 153, 12)); fp->setEnabled(false);
    auto *pk = new QCheckBox("Enable power keys", this); pk->setGeometry(du(7, 148, 153, 12)); pk->setEnabled(false);
    auto *ok = new QPushButton("&OK", this); ok->setGeometry(du(48, 177, 36, 18)); ok->setDefault(true);
    auto *cancel = new QPushButton("&Cancel", this); cancel->setGeometry(du(86, 177, 36, 18));
    auto *help = new QPushButton("&Help", this); help->setGeometry(du(124, 177, 36, 18)); help->setEnabled(false);   /* help topic NOT DECODED */
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

/* OK (FUN_1800da800): the flags; the degrees edit clamped to [1e-6, 90] and made absolute (mastercam_acok.c:30,
   mastercam_acinit.c:41-44). Session-only, as mcamui (the original's persistence is NOT DECODED). */
void AcSettingsDialog::accept() {
    for (int i = 0; i < ACS_N; i++) if (cb_[i]) ac_snap[i] = cb_[i]->isChecked();
    double st = fabs(atof(ang_->text().toUtf8().constData()));   /* atof, as mcamui */ if (st < 1e-6) st = 1e-6; if (st > 90) st = 90; ac_ang_step = st;
    overlay_dirty = 1;
    QDialog::accept();
}
