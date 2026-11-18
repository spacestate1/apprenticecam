#include "homedlg.h"
#include "bpdlg.h"
#include "icons.h"
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QComboBox>
#include <QGroupBox>
#include <cstdio>
#include <cstring>

/* the attribute combos (CBmpCombo2 Add*Images: each item a bitmap of opmanres, the item number + 1 = the value) */
QComboBox *makeAttrCombo(QWidget *parent, int kind) {
    static const int first[3] = { 0x61b7, 0x61bd, 0x61c9 }, count[3] = { 5, 5, 8 };
    auto *c = new QComboBox(parent);
    c->setIconSize(QSize(kind == 2 ? 16 : 56, 12));
    for (int i = 0; i < count[kind]; i++)
        c->addItem(Icons::icon(QString("#file:opmanres_BMP_%1.png").arg(first[kind] + i), kind == 2 ? 16 : 56, 12), QString(), i + 1);
    return c;
}
void setAttrCombo(QComboBox *c, int value) { int i = c->findData(value); if (i >= 0 && c->currentIndex() != i) { c->blockSignals(true); c->setCurrentIndex(i); c->blockSignals(false); } }

static QAbstractButton *noFocus(QAbstractButton *b) { b->setFocusPolicy(Qt::NoFocus); return b; }

/* ------------------------------------------------------------------------------------------------ Change Levels (opmanres 516) */
/* FUN_18046b290: Operation Move (1360) / Copy (1362) = DAT_1809af29c (start 0 = Move), Level Number: Use Active Level (1367) =
   DAT_1808a2620 (start 1) - checked, the number is the main level; else the number 1368 (DAT_1809af2a8) and Select... (1103, the
   Levels picker); Force visibility (4150) On / Off (4151 / 4152): NOT PORTED (greyed). OK -> ChangeEntityLevel's Move / Copy
   (core_ui attr_change_level); Cancel / X -> the selection cleared. Its position: NOT DECODED (centred, ours) */
static bool cl_copy = false, cl_active = true; static int cl_num = 0;
ChangeLevelsDlg::ChangeLevelsDlg(QWidget *parent) : FloatDlg("Change Levels", parent) {
    setObjectName("changeLevels");
    setBodySize(chainDu(this, 0, 0, 129, 175).size());
    QWidget *b = body();
    for (const BsCtl *c = CLDLG_516; c->text; c++) {
        QRect r = chainDu(this, c->x, c->y, c->w, c->h);
        switch (c->id) {
        case 1358: case 1370: { auto *g = new QGroupBox(c->text, b); g->setGeometry(r); g->lower(); break; }
        case 1360: move_ = new QRadioButton(c->text, b); move_->setGeometry(r); break;
        case 1362: copy_ = new QRadioButton(c->text, b); copy_->setGeometry(r); break;
        case 1367: active_ = new QCheckBox(c->text, b); active_->setGeometry(r);
                   connect(active_, &QCheckBox::toggled, this, [this](bool on) { num_->setEnabled(!on); sel_->setEnabled(!on); if (on) num_->setText(QString::number(geo_level)); }); break;
        case 1401: case 444: { auto *l = new QLabel(c->text, b); l->setGeometry(r.adjusted(0, 0, 10, 4)); break; }
        case 1368: num_ = new QLineEdit(b); num_->setGeometry(r); break;
        case 1103: sel_ = new QPushButton(c->text, b); sel_->setGeometry(r);
                   connect(sel_, &QPushButton::clicked, this, [this] {
                       static ChangeLevelsDlg *self; self = this;
                       level_dialog(num_->text().toInt(), [](int lv) { if (self) self->num_->setText(QString::number(lv)); });
                       emit changed(); }); break;
        case 4150: { auto *k = new QCheckBox(c->text, b); k->setGeometry(r); k->setEnabled(false); k->setToolTip("Force visibility: NOT PORTED (levels have no visibility here)"); break; }
        case 4151: case 4152: { auto *k = new QRadioButton(c->text, b); k->setGeometry(r); k->setEnabled(false); break; }
        case 1: { auto *p = new QPushButton(c->text, b); p->setGeometry(r); connect(p, &QPushButton::clicked, this, [this] { finish(true); }); break; }
        case 2: { auto *p = new QPushButton(c->text, b); p->setGeometry(r); connect(p, &QPushButton::clicked, this, [this] { finish(false); }); break; }
        case 9: { auto *p = new QPushButton(c->text, b); p->setGeometry(r); p->setEnabled(false); break; }
        }
    }
    connect(this, &FloatDlg::closeClicked, this, [this] { finish(false); });
    bp_widget_font(this);
}
void ChangeLevelsDlg::start() {
    move_->setChecked(!cl_copy); copy_->setChecked(cl_copy);
    active_->setChecked(cl_active); num_->setText(QString::number(cl_active || cl_num <= 0 ? geo_level : cl_num));
    num_->setEnabled(!cl_active); sel_->setEnabled(!cl_active);
}
void ChangeLevelsDlg::finish(bool ok) {
    hide();
    if (ok) {
        int lv = active_->isChecked() ? geo_level : num_->text().toInt();
        cl_copy = copy_->isChecked(); cl_active = active_->isChecked(); cl_num = lv;
        attr_change_level(cl_copy, lv);
    } else sel_clear();
    overlay_dirty = 1;
    emit changed();
}
void ChangeLevelsDlg::dump() const {
    printf("    chlevelsdlg %s active %d number '%s'\n", copy_->isChecked() ? "copy" : "move", active_->isChecked(), num_->text().toUtf8().constData());
}

/* ------------------------------------------------------------------------------------------------ Attributes (opmanres 192) */
/* sel_attrib_dialog @1800e8aa0 (CAttribs; CAttribs::UpdateEnt applies the checked ones, core_ui attr_set_all): Color 1270 / 1261 (1281
   the Colors dialog), Line Style 1275 / 25269, Point Style 1284 / 25347, Level 1271 / 1265 / Select... 1269, Line Width 1272 / 25273.
   The start values: CAttribs::LoadGlobals (not read) - here the main attributes with every box unchecked (ours). Not ported (greyed):
   1263, Force visibility 4150-4152, Surface Density 1280 / 1287, EA Mgr 25064, 27015, Same as... 1276 */
AttribsDlg::AttribsDlg(QWidget *parent) : FloatDlg("Attributes", parent) {
    setObjectName("attributes");
    setBodySize(chainDu(this, 0, 0, 129, 324).size());
    QWidget *b = body();
    for (const BsCtl *c = ADLG_192; c->text; c++) {
        QRect r = chainDu(this, c->x, c->y, c->w, c->h);
        switch (c->id) {
        case 1259: case 25065: case 25066: case 1268: case 25067: case 1278: { auto *g = new QGroupBox(c->text, b); g->setGeometry(r); g->lower(); break; }
        case 1270: useColor_ = new QCheckBox(b); useColor_->setGeometry(r); break;
        case 1275: useLn_ = new QCheckBox(b); useLn_->setGeometry(r); break;
        case 1284: usePt_ = new QCheckBox(b); usePt_->setGeometry(r); break;
        case 1271: useLevel_ = new QCheckBox(b); useLevel_->setGeometry(r); break;
        case 1272: useW_ = new QCheckBox(b); useW_->setGeometry(r); break;
        case 1261: color_ = new QLineEdit(b); color_->setGeometry(r); break;
        case 1265: level_ = new QLineEdit(b); level_->setGeometry(r); break;
        case 25269: ln_ = makeAttrCombo(b, 0); ln_->setGeometry(r.x(), r.y(), r.width(), 22); break;
        case 25347: pt_ = makeAttrCombo(b, 2); pt_->setGeometry(r.x(), r.y(), r.width(), 22); break;
        case 25273: w_ = makeAttrCombo(b, 1); w_->setGeometry(r.x(), r.y(), r.width(), 22); break;
        case 1281: { auto *p = new QPushButton(b); p->setGeometry(r); p->setText("...");
                     connect(p, &QPushButton::clicked, this, [this] { static AttribsDlg *self; self = this;
                         color_dialog(color_->text().toInt(), 1, [](int c) { if (self) { self->color_->setText(QString::number(c)); self->useColor_->setChecked(true); } }); emit changed(); }); break; }
        case 1269: { auto *p = new QPushButton(c->text, b); p->setGeometry(r);
                     connect(p, &QPushButton::clicked, this, [this] { static AttribsDlg *self; self = this;
                         level_dialog(level_->text().toInt(), [](int lv) { if (self) { self->level_->setText(QString::number(lv)); self->useLevel_->setChecked(true); } }); emit changed(); }); break; }
        case 444: { auto *l = new QLabel(c->text, b); l->setGeometry(r.adjusted(0, 0, 10, 4)); break; }
        case 4150: case 1280: { auto *k = new QCheckBox(c->text, b); k->setGeometry(r); k->setEnabled(false); break; }
        case 4151: case 4152: { auto *k = new QRadioButton(c->text, b); k->setGeometry(r); k->setEnabled(false); break; }
        case 1287: { auto *e = new QLineEdit(b); e->setGeometry(r); e->setEnabled(false); break; }
        case 1263: case 25064: case 27015: case 1276: case 9: { auto *p = new QPushButton(c->text, b); p->setGeometry(r); p->setEnabled(false); break; }
        case 1: { auto *p = new QPushButton(c->text, b); p->setGeometry(r); connect(p, &QPushButton::clicked, this, [this] { finish(true); }); break; }
        case 2: { auto *p = new QPushButton(c->text, b); p->setGeometry(r); connect(p, &QPushButton::clicked, this, [this] { finish(false); }); break; }
        }
    }
    connect(this, &FloatDlg::closeClicked, this, [this] { finish(false); });
    bp_widget_font(this);
}
void AttribsDlg::start() {
    for (QCheckBox *k : { useColor_, useLn_, usePt_, useLevel_, useW_ }) k->setChecked(false);
    color_->setText(QString::number(geo_color)); level_->setText(QString::number(geo_level));
    setAttrCombo(ln_, cfg_ln_style); setAttrCombo(pt_, cfg_pt_style); setAttrCombo(w_, (int)cfg_ln_width);
}
void AttribsDlg::finish(bool ok) {
    hide();
    if (ok) attr_set_all(useColor_->isChecked(), color_->text().toInt(), useLn_->isChecked(), ln_->currentData().toInt(), usePt_->isChecked(), pt_->currentData().toInt(),
                         useLevel_->isChecked(), level_->text().toInt(), useW_->isChecked(), w_->currentData().toInt());
    else sel_clear();
    overlay_dirty = 1;
    emit changed();
}
void AttribsDlg::dump() const {
    printf("    attribsdlg color %d:%s lstyle %d:%d pstyle %d:%d level %d:%s width %d:%d\n", useColor_->isChecked(), color_->text().toUtf8().constData(),
           useLn_->isChecked(), ln_->currentData().toInt(), usePt_->isChecked(), pt_->currentData().toInt(), useLevel_->isChecked(), level_->text().toUtf8().constData(),
           useW_->isChecked(), w_->currentData().toInt());
}
