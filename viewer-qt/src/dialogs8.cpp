#include "dialogs8.h"
#include "textfit.h"
#include "bpdlg.h"
#include "opdlg.h"
#include <QTableWidget>
#include "icons.h"
#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QComboBox>
#include <QGroupBox>
#include <QListWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QTabBar>
#include <QPainter>
#include <QMouseEvent>
#include <cstdio>
#include <cstring>

static QString noAmp(const char *t) { QString s = QString::fromUtf8(t); s.remove('&'); return s; }
static QPixmap resBitmap(const char *mod, int id, int w, int h) { return Icons::icon(QString("#file:%1_BMP_%2.png").arg(mod).arg(id), w, h).pixmap(w, h); }

/* ------------------------------------------------------------------------------------------------ Analyze */
/* mccadutil CAnalyzePoint / CAnalyzeLine / CAnalyzeArc (core_ui az_template): statics (text, or the bitmap statics with the
   runtime replacements az_static_bmp; #1082 = the 2 px separator), edits (az_txt; EN_KILLFOCUS -> Apply = az_apply; the
   read-only ones az_readonly), buttons (the bitmap ones az_button_bmp, OK / Cancel / Help as text; live = az_live, the rest
   greyed), the combos (Style / Width / point style: not modelled, empty and greyed). One entity: Previous / Next / Propagate /
   the separators hidden, every control up 32 px, OK / Cancel / Help 64 px, the window 68 px lower (slot 118, az_ctl_dy_px). Position NOT DECODED: the top
   right of the window (mcamui W - width - 20, TOP_H + 10). */
AnalyzeDialog::AnalyzeDialog(QWidget *parent) : FloatDlg("Analyze", parent) {
    setObjectName("analyze");
    connect(this, &FloatDlg::closeClicked, this, [this] { if (withGL) withGL([] { az_close(0); }); else az_close(0); emit changed(); });   /* X = Cancel (WM_CLOSE -> slot 117) */
}
void AnalyzeDialog::rebuild() {
    filling_ = true;
    for (const Ctl &k : ctls_) { k.w->blockSignals(true); k.w->hide(); k.w->deleteLater(); }   /* a focused edit's focus-out must not apply (editingFinished) */
    ctls_.clear();
    int n, hdlg; const char *title; const BsCtl *c = az_template(&n, &hdlg, &title);
    tmpl_ = c; single_ = az_single();
    setTitle(title);
    { QSize s = chainDu(this, 0, 0, 143, hdlg).size(); setBodySize(QSize(s.width(), s.height() + az_dlg_dh_px())); }
    QWidget *b = body();
    for (int i = 0; i < n; i++) {
        const BsCtl *k = &c[i];
        if (az_hidden(k->id)) continue;
        QRect r = chainDu(this, k->x, k->y, k->w, k->h).translated(0, az_ctl_dy_px(k));
        QWidget *w = nullptr;
        if (k->cls == 0) {
            auto *l = new QLabel(b);
            bool bmp = k->text[0] == '#' && k->text[1] >= '0' && k->text[1] <= '9';   /* "#1082" a bitmap; "# of Operations" (6494) a text */
            if (bmp) { const char *mod; int bm = az_static_bmp(k->text, &mod);
                if (bm == 1082) l->setPixmap(resBitmap(mod, bm, r.width(), 2)); else l->setPixmap(resBitmap(mod, bm, qMin(r.width(), 24), qMin(r.height(), 24))); }
            else l->setText(QString::fromUtf8(k->text));
            l->setGeometry(bmp ? r : r.adjusted(0, 0, 30, 0)); w = l;
        } else if (k->cls == 2) {
            int s = az_slot(k->id); if (s < 0) continue;
            auto *e = new QLineEdit(b); e->setGeometry(r); e->setReadOnly(az_readonly(k->id)); w = e;
            connect(e, &QLineEdit::textEdited, this, [s](const QString &t) { snprintf(az_txt[s], sizeof az_txt[0], "%s", t.toUtf8().constData()); });
            if (!az_readonly(k->id)) connect(e, &QLineEdit::editingFinished, this, [this] { if (filling_) return; if (withGL) withGL([] { az_apply(); }); else az_apply(); emit changed(); });   /* EN_KILLFOCUS -> Apply */
        } else if (k->cls == 1) {
            const char *mod; int bid = az_button_bmp(k->id, &mod);
            QAbstractButton *x;
            if (bid) { auto *t = new QToolButton(b); t->setIcon(QIcon(resBitmap(mod, bid, 16, 16))); t->setIconSize(QSize(16, 16)); x = t; }
            else x = new QPushButton(k->id == 1 ? "OK" : k->id == 2 ? "Cancel" : "Help", b);
            x->setGeometry(r); x->setEnabled(az_live(k->id)); w = x;
            unsigned id = k->id;
            connect(x, &QAbstractButton::clicked, this, [this, id] { if (withGL) withGL([id] { az_act((int)id); }); else az_act((int)id); emit changed(); });
        } else if (k->cls == 6) {                            /* closed combo (the template height is the drop-down): not modelled */
            auto *m = new QComboBox(b); m->setGeometry(r.x(), r.y(), r.width(), chainDu(this, 0, 0, 0, 12).height()); m->setEnabled(false); w = m;
        }
        if (w) { w->show(); ctls_.push_back({ k, w }); }
    }
    bp_widget_font(this);
    filling_ = false;
}
void AnalyzeDialog::refresh() {
    if (az_i < 0 || (az_ent < 0 && az_sol < 0)) return;
    int n, hdlg; const char *title; const BsCtl *c = az_template(&n, &hdlg, &title);
    if (c != tmpl_ || single_ != az_single()) rebuild();
    filling_ = true;
    for (const Ctl &k : ctls_) if (k.c->cls == 2) { auto *e = static_cast<QLineEdit *>(k.w); int s = az_slot(k.c->id); if (s >= 0 && !e->hasFocus()) e->setText(QString::fromUtf8(az_txt[s])); }
    filling_ = false;
}
void AnalyzeDialog::dump() const {
    printf("    azdlg %s:", windowTitle().toUtf8().constData());
    for (const Ctl &k : ctls_) if (k.c->cls == 2) printf(" %u=%s", k.c->id, static_cast<QLineEdit *>(k.w)->text().toUtf8().constData());
    for (const Ctl &k : ctls_) if (k.c->cls == 1) printf(" [%u%s]", k.c->id, k.w->isEnabled() ? "" : " off");
    printf("\n");
}
bool AnalyzeDialog::testSet(const QString &key, const QString &v) {
    for (const Ctl &k : ctls_) {
        if ((int)k.c->id != key.toInt() || !k.w->isEnabled()) continue;
        if (k.c->cls == 2) { auto *e = static_cast<QLineEdit *>(k.w); if (e->isReadOnly()) return false; e->setFocus(); e->setText(v); emit e->textEdited(v); emit e->editingFinished(); return true; }
        if (k.c->cls == 1) { static_cast<QAbstractButton *>(k.w)->click(); return true; }
    }
    return false;
}

/* ------------------------------------------------------------------------------------------------ Colors picker */
/* CColorDlg 593 + CColorPage1 106 (core_ui color_dialog / cdlg_*): "Current color:" edit 226 (typed number: cdlg_commit), the
   swatch 955, Select 213 (enabled when the caller allows an entity pick), FUN_18011a950's 16 x 16 CColorBtn grid (20 px on a
   22 px pitch from 50 px down; a click selects, a double-click selects and closes with OK), OK / Help (greyed); the Customize
   page 108 (palette sliders) is NOT DONE: its tab greyed. The X cancels. */
class ColorGrid : public QWidget {
public:
    explicit ColorGrid(QWidget *parent, ColorPicker *dlg) : QWidget(parent), dlg_(dlg) { setFixedSize(16 * 22, 16 * 22); }
    int at(QPoint p) const { int c = p.x() / 22, r = p.y() / 22; return c >= 0 && c < 16 && r >= 0 && r < 16 && p.x() % 22 < 20 && p.y() % 22 < 20 ? r * 16 + c : -1; }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        for (int i = 0; i < 256; i++) {
            QRect b((i % 16) * 22, (i / 16) * 22, 20, 20); float c[3]; mc_rgb(i, c);
            p.fillRect(b, QColor((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255)));
            p.setPen(QPen(i == cdlg_cur ? QColor(0, 0, 0) : QColor(160, 160, 160), i == cdlg_cur ? 2 : 1)); p.drawRect(b.adjusted(0, 0, -1, -1));
        }
    }
    void mousePressEvent(QMouseEvent *e) override { int i = at(e->position().toPoint()); if (i >= 0) { cdlg_click(i); dlg_->refresh(); } }
    void mouseDoubleClickEvent(QMouseEvent *e) override { int i = at(e->position().toPoint()); if (i >= 0) { cdlg_click(i); cdlg_close(1); emit dlg_->changed(); } }
private:
    ColorPicker *dlg_;
};
ColorPicker::ColorPicker(QWidget *parent) : FloatDlg("Colors", parent) {
    setObjectName("colors");
    QWidget *b = body();
    auto *tabs = new QTabBar(b); tabs->addTab("Colors"); tabs->addTab("Customize"); tabs->setTabEnabled(1, false); tabs->move(0, 0);
    int py = tabs->sizeHint().height() + 4, px = chainDu(this, 4, 0, 0, 0).x();   /* page origin (static 230 at 4,4) */
    auto at = [&](unsigned id) { for (const BsCtl *c = CDLG_593; c->text; c++) if (c->id == id) return chainDu(this, c->x, c->y, c->w, c->h).translated(px, py); return QRect(); };
    auto *l = new QLabel("Current color:", b); l->setGeometry(at(3001));
    cur_ = new QLineEdit(b); cur_->setGeometry(at(226).adjusted(0, 0, 10, 0));
    connect(cur_, &QLineEdit::editingFinished, this, [this] { if (filling_) return; snprintf(cdlg_txt, sizeof cdlg_txt, "%s", cur_->text().toUtf8().constData()); cdlg_commit(); refresh(); });
    swatch_ = new QLabel(b); swatch_->setGeometry(at(955).translated(10, 0));
    sel_ = new QPushButton("Select", b); sel_->setGeometry(at(213).translated(10, 0));
    connect(sel_, &QPushButton::clicked, this, [this] { cdlg_select(); emit changed(); });
    grid_ = new ColorGrid(b, this); grid_->move(px + 2, py + 50);
    int by = py + 50 + 16 * 22 + 8, w = 16 * 22 + px + 12;
    auto *ok = new QPushButton("OK", b), *help = new QPushButton("Help", b); help->setEnabled(false);
    QSize bs = chainDu(this, 0, 0, 36, 18).size();
    ok->setGeometry(w - 2 * (bs.width() + 6), by, bs.width(), bs.height()); help->setGeometry(w - bs.width() - 6, by, bs.width(), bs.height());
    connect(ok, &QPushButton::clicked, this, [this] { cdlg_close(1); emit changed(); });
    connect(this, &FloatDlg::closeClicked, this, [this] { cdlg_close(0); emit changed(); });
    setBodySize(QSize(w, by + bs.height() + 8));
    bp_widget_font(this);
}
void ColorPicker::refresh() {
    filling_ = true;
    if (!cur_->hasFocus()) cur_->setText(QString::fromUtf8(cdlg_txt));
    float c[3]; mc_rgb(cdlg_cur, c);
    swatch_->setStyleSheet(QString("background: rgb(%1,%2,%3); border: 1px solid rgb(120,120,120);").arg((int)(c[0] * 255)).arg((int)(c[1] * 255)).arg((int)(c[2] * 255)));
    sel_->setEnabled(cdlg_select_ok != 0);
    grid_->update();
    filling_ = false;
}
void ColorPicker::dump() const { printf("    colorsdlg '%s' select %d\n", cur_->text().toUtf8().constData(), sel_->isEnabled()); }
bool ColorPicker::testSet(const QString &key, const QString &v) {
    if (key == "click") { cdlg_click(v.toInt()); refresh(); return true; }
    if (key == "txt") { cur_->setFocus(); cur_->setText(v); emit cur_->editingFinished(); cur_->clearFocus(); return true; }
    if (key == "ok") { cdlg_close(1); emit changed(); return true; }
    return false;
}

/* ------------------------------------------------------------------------------------------------ Levels picker */
/* CSelLevel picker mode, opmanres 563 at its template positions (core_ui LDLG_563, level_dialog / ldlg_*): list 1371 (Number /
   Name 1979 / 1948; the main level's row yellow, green when it is the selection, the selection rgb(165,200,238) - mcamui's),
   Selected Level (Number 1359 typed = ldlg_commit, Name 1313 not modelled: greyed, 1327 select entity with bitmap 0x33f),
   List Levels radios (Used / Named / Used or named / Range) with the range edits 744 / 301 (only with Range), Ok / Help */
LevelPicker::LevelPicker(QWidget *parent) : FloatDlg("Levels", parent) {
    setObjectName("levels");
    setBodySize(chainDu(this, 0, 0, 310, 268).size());
    QWidget *b = body();
    for (const BsCtl *c = LDLG_563; c->text; c++) {
        QRect r = chainDu(this, c->x, c->y, c->w, c->h);
        switch (c->id) {
        case 1371: list_ = new QTreeWidget(b); list_->setGeometry(r); list_->setColumnCount(2); list_->setHeaderLabels({ "Number", "Name" }); list_->setRootIsDecorated(false);
                   list_->header()->resizeSection(0, 70);
                   connect(list_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) { if (filling_) return; ldlg_click(it->text(0).toInt()); refresh(); }); break;
        case 1375: case 1376: { auto *g = new QGroupBox(c->text, b); g->setGeometry(r); g->lower(); break; }
        case 1378: case 1379: { auto *l = new QLabel(c->text, b); l->setGeometry(r.adjusted(0, 0, 20, 4)); break; }
        case 1359: num_ = new QLineEdit(b); num_->setGeometry(r);
                   connect(num_, &QLineEdit::editingFinished, this, [this] { if (filling_) return; snprintf(ldlg_txt, sizeof ldlg_txt, "%s", num_->text().toUtf8().constData()); ldlg_commit(); refresh(); }); break;
        case 1313: name_ = new QLineEdit(b); name_->setGeometry(r); name_->setEnabled(false); break;
        case 1327: pick_ = new QToolButton(b); pick_->setGeometry(r); pick_->setIcon(QIcon(resBitmap("opmanres", 0x33f, 16, 16)));
                   connect(pick_, &QToolButton::clicked, this, [this] { ldlg_select(); emit changed(); }); break;
        case 1363: case 1364: case 1365: case 1366: { int k = c->id - 1363; radio_[k] = new QRadioButton(c->text, b); radio_[k]->setGeometry(r.adjusted(0, -2, 0, 2)); radio_[k]->setAutoExclusive(false);
                   connect(radio_[k], &QRadioButton::clicked, this, [this, k] { if (filling_) return; ldlg_radio = k; refresh(); }); break; }
        case 744: lo_ = new QLineEdit(b); lo_->setGeometry(r);
                  connect(lo_, &QLineEdit::editingFinished, this, [this] { if (filling_ || ldlg_radio != 3) return; snprintf(ldlg_lo_t, 16, "%s", lo_->text().toUtf8().constData()); ldlg_range_commit(0); refresh(); }); break;
        case 301: hi_ = new QLineEdit(b); hi_->setGeometry(r);
                  connect(hi_, &QLineEdit::editingFinished, this, [this] { if (filling_ || ldlg_radio != 3) return; snprintf(ldlg_hi_t, 16, "%s", hi_->text().toUtf8().constData()); ldlg_range_commit(1); refresh(); }); break;
        case 1: { auto *p = new QPushButton("Ok", b); p->setGeometry(r); connect(p, &QPushButton::clicked, this, [this] { ldlg_close(1); emit changed(); }); break; }
        case 9: { auto *p = new QPushButton("Help", b); p->setGeometry(r); p->setEnabled(false); break; }
        }
    }
    connect(this, &FloatDlg::closeClicked, this, [this] { ldlg_close(0); emit changed(); });   /* X = Cancel */
    bp_widget_font(this);
}
void LevelPicker::refresh() {
    filling_ = true;
    static int rows[4096]; int nr = ldlg_rows(rows, 4096);
    list_->clear();
    for (int k = 0; k < nr; k++) {
        auto *it = new QTreeWidgetItem(list_, { QString::number(rows[k]), QString() });
        QColor bg = rows[k] == geo_level ? (rows[k] == ldlg_cur ? QColor(0, 255, 0) : QColor(255, 255, 0)) : rows[k] == ldlg_cur ? QColor(165, 200, 238) : QColor();
        if (bg.isValid()) for (int c = 0; c < 2; c++) it->setBackground(c, bg);
    }
    if (!num_->hasFocus()) num_->setText(QString::fromUtf8(ldlg_txt));
    for (int k = 0; k < 4; k++) radio_[k]->setChecked(ldlg_radio == k);
    lo_->setText(QString::fromUtf8(ldlg_lo_t)); hi_->setText(QString::fromUtf8(ldlg_hi_t));
    lo_->setReadOnly(ldlg_radio != 3); hi_->setReadOnly(ldlg_radio != 3);
    filling_ = false;
}
void LevelPicker::dump() const { printf("    levelsdlg '%s' radio %d rows %d\n", num_->text().toUtf8().constData(), ldlg_radio, list_->topLevelItemCount()); }
bool LevelPicker::testSet(const QString &key, const QString &v) {
    if (key == "click") { for (int i = 0; i < list_->topLevelItemCount(); i++) if (list_->topLevelItem(i)->text(0) == v) { emit list_->itemClicked(list_->topLevelItem(i), 0); return true; } return false; }
    if (key == "radio") { radio_[qBound(0, v.toInt(), 3)]->click(); return true; }
    if (key == "txt") { num_->setFocus(); num_->setText(v); emit num_->editingFinished(); num_->clearFocus(); return true; }
    if (key == "ok") { ldlg_close(1); emit changed(); return true; }
    return false;
}

/* ------------------------------------------------------------------------------------------------ Edit Common Parameters */
/* CEditCommonDlg 243 at its template positions (core_ui ECP_243 with each control's binding; OnInitDialog FUN_180143a10, OnOK
   FUN_180143d70, Apply FUN_180143180): a setting's check box enables its edit / choices (FUN_1801417c0 ...); OK / Apply check
   that every enabled field is filled (3786 / 3787) and apply to the selected ops (core ecp_apply -> cam_common_apply); Propagate
   (FUN_180143eb0) needs an op picked in the list; Enable / Disable All Settings (FUN_180143060 / FUN_180142f40). Greyed (not
   done, as mcamui): the pick buttons (Clearance... ...), Coolant / Canned text / Misc. values / Home pos / Planes / NC Name /
   Tool display / Ref point, Select library tool, Tool filter, Help. The op list's columns (COpMgrList) are NOT DECODED: the rows
   read as the Toolpaths tree's (mcamui). */
EcpDialog::EcpDialog(QWidget *parent) : FloatDlg("Edit Common Parameters (Mill/Router)", parent) {
    setObjectName("ecp");
    setBodySize(chainDu(this, 0, 0, 531, 383).size());
    QWidget *b = body();
    for (const EcpCtl *c = ECP_243; c->text; c++) {
        QRect r = chainDu(this, c->x, c->y, c->w, c->h);
        QWidget *w = nullptr;
        switch (c->role) {
        case ECR_GROUP: { auto *g = new QGroupBox(c->text, b); g->setGeometry(r); g->lower(); w = g; break; }
        case ECR_STATIC: { auto *l = new QLabel(c->text, b); l->setGeometry(r); w = l; break; }
        case ECR_CHECK: { auto *x = new QCheckBox(c->text, b); x->setGeometry(r.adjusted(0, 0, c->text[0] ? 0 : 6, 0)); int f = c->f;
                          if (f == -1) x->setEnabled(false);
                          else connect(x, &QCheckBox::clicked, this, [this, f](bool on) { if (filling_) return; int *p = ecp_check_ptr(f); if (p) *p = on; refresh(); });
                          w = x; break; }
        case ECR_EDIT: { auto *e = new QLineEdit(b); e->setGeometry(r); int f = c->f;
                         connect(e, &QLineEdit::textEdited, this, [f](const QString &t) { snprintf(ecp_txt[f], sizeof ecp_txt[f], "%s", t.toUtf8().constData()); });
                         w = e; break; }
        case ECR_COMBO: { auto *m = new QComboBox(b); m->setGeometry(r.x(), r.y(), r.width(), chainDu(this, 0, 0, 0, 14).height()); int f = c->f;
                          if (f <= 0) m->setEnabled(false);
                          else { const char *const *items; int *val, *on; int n = ecp_combo(f, &items, &val, &on); for (int i = 0; i < n; i++) m->addItem(items[i]);
                                 connect(m, QOverload<int>::of(&QComboBox::activated), this, [this, f](int i) { if (filling_) return; const char *const *it; int *val, *on; ecp_combo(f, &it, &val, &on); *val = i; refresh(); }); }
                          w = m; break; }
        case ECR_BTN: { auto *p = new QPushButton(c->text, b); p->setGeometry(r); unsigned id = c->id;
                        if (id == 25055 || id == 25054) {   /* UIBitmapButtons (style BS_BITMAP; mastercam.dll DDX 0x1801410e5, OnInitDialog
                                                               FUN_180143a10 -> 0x180141ea0: opmanres 945 / 943, colour 0xC0C0C0 clear; tooltips
                                                               opmanres 3801 / 3802 at 0x180142e60) - UI-5, the Mastercam-default rule */
                            QPixmap pm = TextFit::keyedBitmap(id == 25055 ? "opmanres_BMP_945.png" : "opmanres_BMP_943.png", 0xC0C0C0);
                            if (!pm.isNull()) { p->setText(QString()); p->setIcon(QIcon(pm)); p->setIconSize(pm.size());
                                                p->setToolTip(id == 25055 ? "Enable all settings" : "Disable all settings"); }
                        }
                        if (c->f == -1) p->setEnabled(false);
                        else connect(p, &QPushButton::clicked, this, [this, id] { if (withGL) withGL([id] { ecp_button((int)id); }); else ecp_button((int)id); emit changed(); });
                        w = p; break; }
        case ECR_LIST: if (c->f == 0) { auto *l = new QListWidget(b); l->setGeometry(r); ops_ = l;
                             connect(l, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) { if (filling_) return; int i = ops_->row(it); ecp_list = ecp_list == i ? -1 : i; refresh(); });
                             w = l; }
                         else {   /* N13-32: list 395 is a CMillToolListCtrl (core_ui cui_ecp.c): Details rows, a header click sorts */
                             auto *t = tools_ = toolTable(b, r);
                             connect(t, &QTableWidget::cellClicked, this, [this](int row, int) { if (filling_ || !ecp_on_toolsel || row < 0 || row >= toolRows_.size()) return;
                                 int i = toolRows_[row]; ecp_tool = ecp_tool == i ? -1 : i; refresh(); });
                             connect(t->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int s) {   /* LVN_COLUMNCLICK: 0x180143b10 -> SortList */
                                 if (filling_ || s < 0 || s >= toolCols_.size()) return;
                                 ecp_sortlist(toolCols_[s]); refresh(); });
                             w = t; }
                         break;
        }
        if (w) ctls_.push_back({ c, w });
    }
    connect(this, &FloatDlg::closeClicked, this, [this] { ecp_cancel(); emit changed(); });   /* the title bar X = Cancel */
    bp_widget_font(this);
}
void EcpDialog::refresh() {
    filling_ = true;
    if (ops_->count() != F.nops) { ops_->clear(); for (int i = 0; i < F.nops; i++) { char t[200]; ecp_op_row(i, t, sizeof t); ops_->addItem(QString::fromUtf8(t)); } }
    { int idx[4096], nr = ecp_tool_rows(idx, 4096), cols[TM_COLS], w[TM_COLS], nc = ecp_tool_cols(cols, w);
      toolRows_.clear(); toolCols_.clear(); for (int i = 0; i < nr; i++) toolRows_.append(idx[i]); for (int k = 0; k < nc; k++) toolCols_.append(cols[k]);
      fillToolRows(tools_, 0, idx, nr, ecp_tool, cols, w, nc, &ecp_cs[2]); }
    for (int i = 0; i < ops_->count(); i++) ops_->item(i)->setSelected(i == ecp_list);
    tools_->setEnabled(ecp_on_toolsel != 0);
    for (const Ctl &k : ctls_) {
        const EcpCtl *c = k.c;
        if (c->role == ECR_CHECK && c->f != -1) { int *p = ecp_check_ptr(c->f); static_cast<QCheckBox *>(k.w)->setChecked(p && *p); }
        else if (c->role == ECR_EDIT) { auto *e = static_cast<QLineEdit *>(k.w); if (!e->hasFocus()) e->setText(QString::fromUtf8(ecp_txt[c->f])); e->setEnabled(*ecp_on_ptr(c->f) != 0); }
        else if (c->role == ECR_COMBO && c->f > 0) { const char *const *it; int *val, *on; ecp_combo(c->f, &it, &val, &on); auto *m = static_cast<QComboBox *>(k.w); m->setCurrentIndex(*val); m->setEnabled(*on != 0); }
        else if (c->role == ECR_BTN && c->id == 348) k.w->setEnabled(ecp_list >= 0);   /* Propagate: with a list selection (FUN_180144520) */
    }
    filling_ = false;
}
void EcpDialog::dump() const {
    printf("    ecpdlg:");
    for (const Ctl &k : ctls_) {
        if (k.c->role == ECR_CHECK && k.c->f != -1 && static_cast<QCheckBox *>(k.w)->isChecked()) printf(" [%u]", k.c->id);
        if (k.c->role == ECR_EDIT && k.w->isEnabled()) printf(" %u=%s", k.c->id, static_cast<QLineEdit *>(k.w)->text().toUtf8().constData());
        if (k.c->role == ECR_COMBO && k.w->isEnabled()) printf(" %u#%d", k.c->id, static_cast<QComboBox *>(k.w)->currentIndex());
    }
    printf(" list %d tool %d\n", ecp_list, ecp_tool);
}
bool EcpDialog::testSet(const QString &key, const QString &v) {
    if (key == "op") { int i = v.toInt() - 1; if (i < 0 || i >= ops_->count()) return false; emit ops_->itemClicked(ops_->item(i)); return true; }
    if (key == "tool") { int i = v.toInt(); if (i < 0 || i >= tools_->rowCount() || !tools_->isEnabled()) return false; emit tools_->cellClicked(i, 0); return true; }   /* tool=ROW */
    if (key == "sort") { int s = toolCols_.indexOf(v.toInt()); if (s < 0 || !tools_->isEnabled()) return false; emit tools_->horizontalHeader()->sectionClicked(s); return true; }   /* N13-32: sort=C */
    for (const Ctl &k : ctls_) {
        if ((int)k.c->id != key.toInt() || !k.w->isEnabled()) continue;
        if (k.c->role == ECR_CHECK || k.c->role == ECR_BTN) { static_cast<QAbstractButton *>(k.w)->click(); return true; }
        if (k.c->role == ECR_EDIT) { auto *e = static_cast<QLineEdit *>(k.w); e->setText(v); emit e->textEdited(v); return true; }
        if (k.c->role == ECR_COMBO) { auto *m = static_cast<QComboBox *>(k.w); m->setCurrentIndex(v.toInt()); emit m->activated(v.toInt()); return true; }
    }
    return false;
}

/* ------------------------------------------------------------------------------------------------ Screen Grid (1589) */
/* the Configuration page's controls (core_ui GS_1589; drawn at x - 140, y - 15 du as mcamui): Spacing X / Y, Origin X / Y +
   Select (25203: picking NOT PORTED, greyed), Snap Near / Always, Size; a typed number applies while typed (gs_edit) */
/* opmanres dialog 977 (266 x 126 du; template dumped with tools/pe_dialogs.py): the static text (7,7 252x39), radios 25350 "Regenerate
   unlocked operations and continue (Locked operations will not be regenerated and will be processed as is)" (21,52 224x22) and
   25351 "Do not regenerate and continue" (21,83 213x10), OK 1 (146,101), Cancel 2 (184,101), Help 9 (222,101) 36x18. CDirtyOpsDlg
   (mastercam.dll FUN_18012f020): DDX_Radio(25350, +0x1550), OnInitDialog sets +0x1550 = 0 (Regenerate). Help greyed (Mastercam's
   help file). Modal in Mastercam (DoModal from COpMgrTree::DirtyOperationWarning); here it holds the command (dop_pending) and the
   main window shows it while the command waits. */
DirtyOpsDialog::DirtyOpsDialog(QWidget *parent) : FloatDlg("Warning: Dirty Operations Selected", parent) {
    setObjectName("dirtyops977");
    setClosable(false);
    setBodySize(chainDu(this, 0, 0, 266, 126).size());
    QWidget *b = body();
    auto *t = new QLabel("One or more of the selected operations are dirty and may require regeneration.  Dirty operations will not "
                         "accurately represent the Operation Parameters settings.  Choose an option:", b);
    t->setWordWrap(true); t->setAlignment(Qt::AlignLeft | Qt::AlignTop); t->setGeometry(chainDu(this, 7, 7, 252, 39));
    regen_ = new QRadioButton(b); regen_->setGeometry(chainDu(this, 21, 52, 224, 22));
    { auto *l = new QLabel("Regenerate unlocked operations and continue (Locked operations will not be regenerated and will be processed as is)", b);
      l->setWordWrap(true); l->setAlignment(Qt::AlignLeft | Qt::AlignTop); QRect r = chainDu(this, 21, 52, 224, 22); l->setGeometry(r.adjusted(18, -3, 6, 10)); l->setBuddy(regen_);
      l->setObjectName("r25350"); }   /* the multi-line radio (two lines in 22 du): its text as a label beside the button (ours) */
    noregen_ = new QRadioButton("Do not regenerate and continue", b); noregen_->setGeometry(chainDu(this, 21, 83, 213, 10).adjusted(0, -2, 0, 2));
    regen_->setAutoExclusive(false); noregen_->setAutoExclusive(false);
    connect(regen_, &QRadioButton::clicked, this, [this] { if (filling_) return; dop_choice = 0; refresh(); });
    connect(noregen_, &QRadioButton::clicked, this, [this] { if (filling_) return; dop_choice = 1; refresh(); });
    auto *ok = new QPushButton("OK", b); ok->setGeometry(chainDu(this, 146, 101, 36, 18)); ok->setDefault(true);
    auto *cancel = new QPushButton("Cancel", b); cancel->setGeometry(chainDu(this, 184, 101, 36, 18));
    auto *help = new QPushButton("Help", b); help->setGeometry(chainDu(this, 222, 101, 36, 18)); help->setEnabled(false);
    connect(ok, &QPushButton::clicked, this, [this] { emit answered(1); });
    connect(cancel, &QPushButton::clicked, this, [this] { emit answered(0); });
    bp_widget_font(this);
}
void DirtyOpsDialog::refresh() {
    filling_ = true;
    regen_->setChecked(dop_choice == 0); noregen_->setChecked(dop_choice == 1);
    filling_ = false;
}

GridDialog::GridDialog(QWidget *parent) : FloatDlg("Configuration - Screen Grid", parent) {
    setObjectName("gridset");
    setBodySize(chainDu(this, 0, 0, 135, 210).size());
    QWidget *b = body();
    for (const GsCtl *c = GS_1589; c->text; c++) {
        QRect r = chainDu(this, c->x - 140, c->y - 15, c->w, c->h);
        switch (c->t) {
        case 'g': { auto *g = new QGroupBox(c->text, b); g->setGeometry(r); g->lower(); break; }
        case 's': { auto *l = new QLabel(c->text, b); l->setGeometry(r.adjusted(0, -2, 8, 2)); break; }
        case 'e': { int k = c->k; ed_[k] = new QLineEdit(b); ed_[k]->setGeometry(r);
                    connect(ed_[k], &QLineEdit::textEdited, this, [this, k](const QString &t) { if (!gs_inited) gs_fill(); snprintf(gs_txt[k], sizeof gs_txt[k], "%s", t.toUtf8().constData()); gs_edit(k); emit changed(); }); break; }
        case 'r': { auto *x = new QRadioButton(c->text, b); x->setGeometry(r.adjusted(0, -2, 0, 2)); x->setAutoExclusive(false); int m = c->k;
                    (m == 1 ? near_ : always_) = x;
                    connect(x, &QRadioButton::clicked, this, [this, m] { if (filling_) return; grid.snap_mode = m; refresh(); emit changed(); }); break; }
        case 'b': { auto *p = new QPushButton(b); p->setGeometry(r); p->setEnabled(false); break; }
        }
    }
    connect(this, &FloatDlg::closeClicked, this, [this] { gs_close(); emit changed(); });
    bp_widget_font(this);
}
void GridDialog::refresh() {
    filling_ = true;
    if (!gs_inited) gs_fill();
    for (int k = 0; k < 5; k++) if (!ed_[k]->hasFocus()) ed_[k]->setText(QString::fromUtf8(gs_txt[k]));
    near_->setChecked(grid.snap_mode == 1); always_->setChecked(grid.snap_mode == 2);
    filling_ = false;
}
bool GridDialog::testSet(const QString &key, const QString &v) {
    if (key == "snap") { (v.toInt() == 1 ? near_ : always_)->click(); return true; }
    int k = key.toInt(); if (k < 0 || k > 4) return false;
    ed_[k]->setText(v); emit ed_[k]->textEdited(v); return true;
}

/* ------------------------------------------------------------------------------------------------ About / keymap (ours) */
AboutWindow::AboutWindow(QWidget *parent) : FloatDlg("About apprenticeCAM", parent) {
    setObjectName("about");
    setBodySize(QSize(440, 180 - 26));
    auto *l = new QLabel(ABOUT_TEXT, body()); l->setWordWrap(true); l->setGeometry(8, 8, 424, 60);
    auto *ok = new QPushButton("OK", body()); ok->setGeometry(440 - 8 - 140, 180 - 26 - 40, 140, 28);
    connect(ok, &QPushButton::clicked, this, [this] { show_about = 0; emit changed(); });
    connect(this, &FloatDlg::closeClicked, this, [this] { show_about = 0; emit changed(); });
}
/* INTENTIONAL (formats/ui.md "Keyboard window"): OnHelp opens Mastercam's CHM help, which is not ported; this window lists the
   user's keymap with each function's own menu label (cmd_labels.tsv), as mcamui's "Keyboard shortcuts" */
KeysWindow::KeysWindow(QWidget *parent, const std::function<QString(const QString &)> &label) : FloatDlg("Keyboard shortcuts", parent), label_(label) {
    setObjectName("keys");
    src_ = new QLabel(body()); src_->setStyleSheet("color: rgb(20,70,140);"); src_->setGeometry(6, 4, 588, 18);
    list_ = new QTreeWidget(body()); list_->setColumnCount(2); list_->setHeaderHidden(true); list_->setRootIsDecorated(false);
    connect(this, &FloatDlg::closeClicked, this, [this] { show_help = 0; emit changed(); });
}
void KeysWindow::refresh() {
    QWidget *p = parentWidget(); int h = p ? qMax(300, p->height() - 152 - 80) : 600;
    setBodySize(QSize(600, h - 26)); list_->setGeometry(4, 26, 592, h - 26 - 30);
    src_->setText(QString("Keymap: %1").arg(QString::fromUtf8(kmap_src)));
    list_->clear();
    for (int i = 0; i < cui_nkmap; i++) new QTreeWidgetItem(list_, { QString::fromUtf8(cui_kmap[i].keys), label_(QString::fromUtf8(cui_kmap[i].fn)) });
    list_->header()->resizeSection(0, 290);
}
