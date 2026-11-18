#include "lfacedlg.h"
#include "bpdlg.h"
#include "textfit.h"
#include <QTabBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QComboBox>
#include <QGroupBox>
#include <QTextEdit>
#include <cstdio>
#include <cstring>


/* the sheet (CLTpSheet): the tab strip (the tool page's tab text = mclatheres 2093 "Toolpath parameters", the Face page's = its
   template title "Face Parameters"), the page (the larger template, 455 x 317 du), OK / Cancel (the property sheet's; Help and
   the sheet's own caption NOT DECODED: the caption is the op type's name) */
LfaceDialog::LfaceDialog(QWidget *parent) : FloatDlg("Lathe Face", parent) {
    setObjectName("lface");
    QRect pg = chainDu(this, 0, 0, 455, 317);
    int tabH = 26, btnH = chainDu(this, 0, 0, 36, 18).height();
    setBodySize(QSize(pg.width() + 8, tabH + pg.height() + btnH + 16));
    QWidget *b = body();
    tabs_ = new QTabBar(b); tabs_->addTab("Toolpath parameters"); tabs_->addTab("Face Parameters"); tabs_->setGeometry(4, 2, pg.width(), tabH);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int i) { if (filling_) return; run([i] { lface_set_page(i); }); });
    for (int k = 0; k < 2; k++) { page_[k] = new QWidget(b); page_[k]->setGeometry(4, tabH + 2, pg.width(), pg.height()); }
    for (const LfCtl *c = LF_CTLS; c->text; c++) {
        QWidget *p = page_[c->page];
        QRect r = chainDu(this, c->x, c->y, c->w, c->h);
        QString t = QString::fromUtf8(c->text);
        QWidget *w = nullptr;
        switch (c->role) {
        case LFR_GROUP: { auto *g = new QGroupBox(t, p); g->setGeometry(r); g->lower(); w = g; break; }
        case LFR_STATIC: { auto *l = new QLabel(t, p); l->setGeometry(r); w = l; break; }
        case LFR_LIST: {
            auto *tw = list_ = new QTableWidget(0, 4, p); tw->setGeometry(r);
            QStringList h; for (int k = 0; k < 4; k++) h << QString::fromUtf8(lface_tool_col_name(k));
            tw->setHorizontalHeaderLabels(h); tw->verticalHeader()->hide(); tw->setSelectionBehavior(QAbstractItemView::SelectRows);
            tw->setSelectionMode(QAbstractItemView::SingleSelection); tw->setEditTriggers(QAbstractItemView::NoEditTriggers);
            int cw[4] = { 56, 135, 62, 44 };                  /* ReadToolPageDisplayInfo 0x180177900 defaults (no registry key) */
            for (int k = 0; k < 4; k++) tw->setColumnWidth(k, cw[k]);
            connect(tw, &QTableWidget::cellClicked, this, [this](int row, int) { if (filling_) return; run([row] { lface_pick_tool(row); }); });
            w = tw; break; }
        case LFR_CHECK: case LFR_ROUGH: case LFR_FINISH: case LFR_AWAY: case LFR_NA_CHECK: {
            auto *x = new QCheckBox(t, p); x->setGeometry(r.adjusted(0, 0, t.isEmpty() ? 6 : 0, 0));
            connect(x, &QCheckBox::clicked, this, [this, c](bool on) { if (filling_) return; run([c, on] { lface_ctl_set(c, on ? "1" : "0"); }); });
            w = x; break; }
        case LFR_SRCRADIO: case LFR_FEEDU: case LFR_FFEEDU: case LFR_CSS: case LFR_RPMR: case LFR_NA_RADIO: {
            auto *x = new QRadioButton(t, p); x->setGeometry(r.adjusted(0, 0, t.isEmpty() ? 6 : 0, 0)); x->setAutoExclusive(false);
            connect(x, &QRadioButton::clicked, this, [this, c] { if (filling_) return; run([c] { lface_ctl_set(c, "1"); }); });
            w = x; break; }
        case LFR_I32: case LFR_I16: case LFR_F64: case LFR_FEED: case LFR_RPM: case LFR_FINZ: {
            auto *e = new QLineEdit(p); e->setGeometry(r);
            connect(e, &QLineEdit::editingFinished, this, [this, c, e] { if (filling_) return; QByteArray v = e->text().toUtf8(); run([c, v] { lface_ctl_set(c, v.constData()); }); });
            w = e; break; }
        case LFR_COMMENT: {
            auto *e = new QLineEdit(p); e->setGeometry(r); e->setMaxLength(0x77);
            connect(e, &QLineEdit::editingFinished, this, [this, c, e] { if (filling_) return; QByteArray v = e->text().toUtf8(); run([c, v] { lface_ctl_set(c, v.constData()); }); });
            w = e; break; }
        case LFR_COMBO_DIR: case LFR_COMBO_ROLL: case LFR_NA_COMBO: {
            auto *m = new QComboBox(p); m->setGeometry(r.x(), r.y(), r.width(), chainDu(this, 0, 0, 0, 14).height());
            const char *const *it; int n = lface_combo(c, &it); for (int i = 0; i < n; i++) m->addItem(it[i]);
            connect(m, QOverload<int>::of(&QComboBox::activated), this, [this, c](int i) { if (filling_) return; char v[16]; snprintf(v, sizeof v, "%d", i); std::string s(v); run([c, s] { lface_ctl_set(c, s.c_str()); }); });
            w = m; break; }
        case LFR_BTN: case LFR_NA: {
            auto *pb = new QPushButton(QString(t).replace("&", "&&").replace("&&", "&"), p); pb->setGeometry(r);
            connect(pb, &QPushButton::clicked, this, [this, c] { run([c] { lface_ctl_set(c, ""); }); });
            w = pb; break; }
        }
        if (w) ctls_.push_back({ c, w });
    }
    int y = tabH + pg.height() + 8, bw = chainDu(this, 0, 0, 50, 0).width();
    auto *ok = new QPushButton("OK", b); ok->setGeometry(pg.width() - 2 * bw - 6, y, bw, btnH);
    auto *cancel = new QPushButton("Cancel", b); cancel->setGeometry(pg.width() - bw, y, bw, btnH);
    connect(ok, &QPushButton::clicked, this, [this] { run([] { lface_ok(); }); });
    connect(cancel, &QPushButton::clicked, this, [this] { run([] { lface_cancel(); }); });
    connect(this, &FloatDlg::closeClicked, this, [this] { run([] { lface_cancel(); }); });
    bp_widget_font(this);                                 /* the templates' font, MS Shell Dlg 8 = 11 px (the style sheet stops the inheritance; textfit, 2026-11-17) */
}
void LfaceDialog::run(const std::function<void()> &fn) { if (withGL) withGL(fn); else fn(); emit changed(); }
void LfaceDialog::refresh() {
    if (!show_lface) return;
    filling_ = true;
    tabs_->setCurrentIndex(lface_page);
    for (int k = 0; k < 2; k++) page_[k]->setVisible(k == lface_page);
    int nr = lface_tool_rows();
    list_->setRowCount(nr);
    for (int r = 0; r < nr; r++) for (int k = 0; k < 4; k++) { char t[200]; lface_tool_cell(r, k, t, sizeof t); list_->setItem(r, k, new QTableWidgetItem(QString::fromUtf8(t))); }
    TextFit::fitColumns(list_);                       /* ours, user decision UI-2: the columns fit their header / cells */
    if (lface_tool_row() >= 0) list_->selectRow(lface_tool_row());
    for (const Ctl &k : ctls_) {
        const LfCtl *c = k.c; char t[200];
        if (c->role == LFR_STATIC || c->role == LFR_GROUP) continue;
        k.w->setEnabled(lface_ctl_enabled(c) || c->role == LFR_LIST);
        if (c->role == LFR_LIST) continue;
        if (auto *ab = qobject_cast<QAbstractButton *>(k.w); ab && ab->isCheckable()) ab->setChecked(lface_ctl_checked(c));
        else if (auto *e = qobject_cast<QLineEdit *>(k.w)) {
            lface_ctl_text(c, t, sizeof t); if (!e->hasFocus()) e->setText(QString::fromUtf8(t));
            QFont fo = e->font(); fo.setItalic(lface_ctl_italic(c) != 0); e->setFont(fo);   /* a machine check changed it (L-12, WM_SETFONT italic) */
        }
        else if (auto *m = qobject_cast<QComboBox *>(k.w)) {
            if (c->role == LFR_NA_COMBO) { lface_ctl_text(c, t, sizeof t); m->clear(); m->addItem(QString::fromUtf8(t)); }
            else m->setCurrentIndex(lface_combo_index(c)); }
    }
    filling_ = false;
}
