/* Transform > Geometry Nesting's windows (core_ui cui_nest.c has the templates, the model and every citation). The property
   sheet CNestPropSht: its page tabs over the page area, the pages Sheets (101) and Parts (105) at their template positions
   (dialog units as the other template dialogs, chainDu), the sheet's bitmap buttons and OK / Cancel / Help below. The
   Configuration dialog (201) and the Nesting Results dialog (205) are one template each. A list box's right-click opens its
   menu resource (601 parts, 609 sheets). Ours (labelled): the bitmap buttons show the first letter of their tooltip (the
   mcnestingres bitmaps are not extracted), the owner-drawn previews (3023 / 4018 / 8019) are empty frames (their drawing is
   NOT DECODED). */
#include "nestdlg.h"
#include "bpdlg.h"
extern "C" {
#include "core_ui/core_ui.h"
}
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QTabBar>
#include <QFrame>
#include <cstdio>

namespace {
QString noAmp(const char *t) { QString s = QString::fromUtf8(t ? t : ""); if (s.startsWith('|')) s = s.mid(1); s.remove('&'); return s; }
int slot(int tpl) { return tpl == 101 ? 1 : tpl == 105 ? 2 : 0; }
}

NestDialog::NestDialog(int tpl, QWidget *parent) : FloatDlg("", parent), tpl_(tpl) {
    setObjectName(tpl == NS_SHEET ? "nestSheet" : tpl == 201 ? "nestCfg" : "nestResults");
    int n = 0, w = 0, h = 0; ns_table(tpl, &n, &w, &h);
    QRect pr = chainDu(this, 0, 0, w, h);
    setBodySize(pr.size());
    setTitle(QString::fromUtf8(ns_text(tpl, 0xffffffffu)));      /* the window title (ns_text of the TITLE pseudo id) */
    QWidget *b = body();
    if (tpl == NS_SHEET) {
        const NsCtl *t = ns_table(tpl, &n, &w, &h);
        for (int i = 0; i < n; i++) if (t[i].id == 0) {           /* the page area: the tab bar above it, the pages in it */
            QRect r = chainDu(this, t[i].x, t[i].y, t[i].w, t[i].h);
            tabs_ = new QTabBar(b); tabs_->setGeometry(r.x(), r.y() - 24, r.width(), 24); tabs_->setDrawBase(true);
            tabs_->addTab(QString::fromUtf8(ns_text(101, 0xffffffffu))); tabs_->addTab(QString::fromUtf8(ns_text(105, 0xffffffffu)));
            connect(tabs_, &QTabBar::currentChanged, this, [this](int k) { if (filling_) return; ns_page = k; refresh(); emit changed(); });
            for (int k = 0; k < 2; k++) { pages_[k] = new QWidget(b); pages_[k]->setGeometry(r); build(k ? 105 : 101, pages_[k]);
                pages_[k]->setContextMenuPolicy(Qt::CustomContextMenu);   /* the page background: menu 610 (Save / Load defaults) */
                QWidget *pg = pages_[k];
                connect(pg, &QWidget::customContextMenuRequested, this, [this, pg](const QPoint &p) { popupMenu(NS_SHEET, 0, pg->mapToGlobal(p)); }); }
        }
    }
    build(tpl, b);
    connect(this, &FloatDlg::closeClicked, this, [this] { int t = tpl_; gl([t] { ns_click(t, 2); }); emit changed(); });   /* the close box = Cancel (IDCANCEL) */
    bp_widget_font(this);
}

void NestDialog::build(int tpl, QWidget *page) {
    int n = 0, w = 0, h = 0; const NsCtl *t = ns_table(tpl, &n, &w, &h);
    QMap<unsigned, QWidget *> &wm = w_[slot(tpl)];
    for (int i = 0; i < n; i++) {
        const NsCtl *c = &t[i]; if (tpl == NS_SHEET && c->id == 0) continue;
        QRect r = chainDu(this, c->x, c->y, c->w, c->h); unsigned id = c->id; QWidget *x = nullptr;
        switch (c->role) {
        case NSR_GROUP: { auto *g = new QGroupBox(noAmp(c->text), page); g->setGeometry(r); g->lower(); x = g; } break;
        case NSR_STATIC: { auto *l = new QLabel(noAmp(c->text), page); l->setGeometry(r.adjusted(0, -2, 10, 2)); x = l; } break;
        case NSR_VALUE: { auto *l = new QLabel(page); l->setGeometry(r); l->setFrameShape(QFrame::Panel); l->setFrameShadow(QFrame::Sunken);
            l->setAlignment(Qt::AlignCenter); x = l; } break;
        case NSR_LIST: { auto *l = new QListWidget(page); l->setGeometry(r); l->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(l, &QListWidget::currentRowChanged, this, [this, tpl, id](int row) { if (filling_ || row < 0) return; ns_select(tpl, id, row); refresh(); emit changed(); });
            connect(l, &QWidget::customContextMenuRequested, this, [this, tpl, id, l](const QPoint &p) { popupMenu(tpl, id, l->mapToGlobal(p)); });
            x = l; } break;
        case NSR_EDIT: { auto *e = new QLineEdit(page); e->setGeometry(r);
            connect(e, &QLineEdit::editingFinished, this, [this, e, tpl, id] { if (filling_) return; ns_edit(tpl, id, e->text().toUtf8().constData()); refresh(); emit changed(); });
            x = e; } break;
        case NSR_COMBO: case NSR_DROP: { auto *m = new QComboBox(page); m->setGeometry(r.x(), r.y(), r.width(), chainDu(this, 0, 0, 0, 12).height() + 2);
            m->setEditable(c->role == NSR_COMBO);
            connect(m, QOverload<int>::of(&QComboBox::activated), this, [this, tpl, id](int k) { if (filling_) return; ns_select(tpl, id, k); refresh(); emit changed(); });
            if (m->lineEdit()) connect(m->lineEdit(), &QLineEdit::editingFinished, this, [this, m, tpl, id] { if (filling_) return; ns_edit(tpl, id, m->currentText().toUtf8().constData()); refresh(); emit changed(); });
            x = m; } break;
        case NSR_CHECK: { auto *k = new QCheckBox(noAmp(c->text), page); k->setGeometry(r.adjusted(0, -2, 14, 2));
            connect(k, &QCheckBox::clicked, this, [this, tpl, id] { if (filling_) return; gl([tpl, id] { ns_click(tpl, id); }); refresh(); emit changed(); }); x = k; } break;
        case NSR_RADIO: { auto *k = new QRadioButton(noAmp(c->text), page); k->setAutoExclusive(false); k->setGeometry(r.adjusted(0, -2, 14, 2));
            connect(k, &QRadioButton::clicked, this, [this, tpl, id] { if (filling_) return; gl([tpl, id] { ns_click(tpl, id); }); refresh(); emit changed(); }); x = k; } break;
        case NSR_BUTTON: case NSR_BMPBTN: { auto *p = new QPushButton(page); p->setGeometry(r);
            QString tip = noAmp(c->role == NSR_BMPBTN ? ns_tip(tpl, id) : c->text);
            if (c->role == NSR_BMPBTN && c->w < 30) { p->setText(tip.left(1)); p->setToolTip(tip); } else p->setText(tip);   /* ours: the letter (no bitmaps); OK / Cancel / Help their text */
            connect(p, &QPushButton::clicked, this, [this, tpl, id] { gl([tpl, id] { ns_click(tpl, id); }); if (isVisible()) refresh(); emit changed(); }); x = p; } break;
        case NSR_OWNER: case NSR_SWATCH: { auto *f = new QFrame(page); f->setGeometry(r); f->setFrameShape(QFrame::Box); f->lower(); x = f; } break;   /* ours: empty */
        default: break;
        }
        if (!x) continue;
        x->show();
        if (id != 0xffffffffu) wm[id] = x;
    }
}

void NestDialog::fill(int tpl, unsigned id, QWidget *w) {
    w->setVisible(ns_shown(tpl, id)); w->setEnabled(ns_enabled(tpl, id));
    if (auto *k = qobject_cast<QCheckBox *>(w)) k->setChecked(ns_checked(tpl, id));
    else if (auto *k = qobject_cast<QRadioButton *>(w)) k->setChecked(ns_checked(tpl, id));
    else if (auto *e = qobject_cast<QLineEdit *>(w)) { if (!e->hasFocus()) e->setText(QString::fromUtf8(ns_text(tpl, id))); }
    else if (auto *l = qobject_cast<QListWidget *>(w)) {
        const char **it = nullptr; int n = ns_items(tpl, id, &it);
        QStringList want; for (int i = 0; i < n; i++) want << QString::fromUtf8(it[i]);
        QStringList have; for (int i = 0; i < l->count(); i++) have << l->item(i)->text();
        if (want != have) { l->clear(); l->addItems(want); }
        l->setCurrentRow(ns_cursel(tpl, id));
    } else if (auto *m = qobject_cast<QComboBox *>(w)) {
        const char **it = nullptr; int n = ns_items(tpl, id, &it);
        QStringList want; for (int i = 0; i < n; i++) want << QString::fromUtf8(it[i]);
        QStringList have; for (int i = 0; i < m->count(); i++) have << m->itemText(i);
        if (want != have) { m->clear(); m->addItems(want); }
        if (m->isEditable()) { if (!m->lineEdit()->hasFocus()) m->setEditText(QString::fromUtf8(ns_text(tpl, id))); }
        else m->setCurrentIndex(ns_cursel(tpl, id));
    } else if (auto *v = qobject_cast<QLabel *>(w)) {
        const char *t = ns_text(tpl, id); if (t) v->setText(QString::fromUtf8(t));   /* NULL: the template's text */
    }
}

void NestDialog::refresh() {
    filling_ = true;
    setTitle(QString::fromUtf8(ns_text(tpl_, 0xffffffffu)));
    if (tabs_) { tabs_->setCurrentIndex(ns_page); for (int k = 0; k < 2; k++) pages_[k]->setVisible(k == ns_page); }
    for (int s = 0; s < 3; s++) {
        int tpl = s == 1 ? 101 : s == 2 ? 105 : tpl_;
        for (auto it = w_[s].begin(); it != w_[s].end(); ++it) fill(tpl, it.key(), it.value());
    }
    filling_ = false;
}

void NestDialog::popupMenu(int tpl, unsigned id, QPoint global) {
    const int *cmds = nullptr; int n = 0; const char *const *t = ns_menu(tpl, id, &cmds, &n);
    if (!t) return;
    QMenu m(this); QMenu *sub = nullptr;
    for (int i = 0; i < n; i++) {
        QString s = QString::fromUtf8(t[i]);
        if (s.isEmpty()) { (sub ? sub : &m)->addSeparator(); continue; }
        if (s.startsWith('>')) { sub = m.addMenu(s.mid(1)); continue; }   /* a popup: its items follow until "<" */
        if (s == "<") { sub = nullptr; continue; }
        QAction *a = (sub ? sub : &m)->addAction(s);
        int cmd = cmds[i]; a->setEnabled(ns_menu_enabled(tpl, cmd));
        connect(a, &QAction::triggered, this, [this, tpl, cmd] { gl([tpl, cmd] { ns_menu_cmd(tpl, cmd); }); if (isVisible()) refresh(); emit changed(); });
    }
    m.exec(global);
}

bool NestDialog::testSet(const QString &key, const QString &v) {
    if (key == "tab") { if (!tabs_) return false; int k = v.toInt(); if (k < 0 || k > 1) return false; tabs_->setCurrentIndex(k); return true; }
    if (key == "menu") {                                  /* menu=LIST,CMD: a list's right-click menu command */
        unsigned id = v.section(',', 0, 0).toUInt(); int cmd = v.section(',', 1, 1).toInt();
        int tpl = tpl_ == NS_SHEET ? (id == 0 ? NS_SHEET : ns_page ? 105 : 101) : tpl_;   /* id 0: the page background */
        const int *cmds = nullptr; int n = 0; if (!ns_menu(tpl, id, &cmds, &n)) return false;
        for (int i = 0; i < n; i++) if (cmds[i] == cmd && ns_menu_enabled(tpl, cmd)) { gl([tpl, cmd] { ns_menu_cmd(tpl, cmd); }); if (isVisible()) refresh(); emit changed(); return true; }
        return false;
    }
    unsigned id = key.toUInt();
    QWidget *w = nullptr;
    int s = tpl_ == NS_SHEET ? (ns_page ? 2 : 1) : 0;
    if (w_[s].contains(id)) w = w_[s][id]; else if (w_[0].contains(id)) w = w_[0][id];
    if (!w || !w->isVisible() || !w->isEnabled()) return false;
    if (auto *e = qobject_cast<QLineEdit *>(w)) { e->setText(v); emit e->editingFinished(); return true; }
    if (auto *l = qobject_cast<QListWidget *>(w)) { int r = v.toInt(); if (r < 0 || r >= l->count()) return false; l->setCurrentRow(r); return true; }
    if (auto *m = qobject_cast<QComboBox *>(w)) {
        if (m->isEditable() && !v.startsWith('#')) { m->setEditText(v); emit m->lineEdit()->editingFinished(); return true; }
        int k = v.mid(v.startsWith('#') ? 1 : 0).toInt(); if (k < 0 || k >= m->count()) return false;
        m->setCurrentIndex(k); emit m->activated(k); return true;
    }
    if (auto *x = qobject_cast<QAbstractButton *>(w)) { x->click(); return true; }
    return false;
}

void NestDialog::dump() const {
    const char *nm = tpl_ == NS_SHEET ? "nestdlg" : tpl_ == 201 ? "nestcfg" : "nestres";
    printf("    %s '%s'", nm, windowTitle().toUtf8().constData());
    if (tabs_) printf(" page %d", ns_page);
    printf(":");
    for (int s = 0; s < 3; s++) {
        if (tpl_ == NS_SHEET && s && s != (ns_page ? 2 : 1)) continue;
        for (auto it = w_[s].begin(); it != w_[s].end(); ++it) {
            unsigned id = it.key(); QWidget *w = it.value(); if (!w->isVisible()) continue;
            const char *g = w->isEnabled() ? "" : "(g)";
            if (auto *x = qobject_cast<QAbstractButton *>(w); x && x->isCheckable()) printf(" %u%s%s", id, x->isChecked() ? "[x]" : "[ ]", g);
            else if (auto *e = qobject_cast<QLineEdit *>(w)) printf(" %u=%s%s", id, e->text().toUtf8().constData(), g);
            else if (auto *m = qobject_cast<QComboBox *>(w)) printf(" %u=%s%s", id, m->currentText().toUtf8().constData(), g);
            else if (auto *l = qobject_cast<QListWidget *>(w)) { printf(" %u={", id); for (int i = 0; i < l->count(); i++) printf("%s%s%s", i ? "|" : "", i == l->currentRow() ? "*" : "", l->item(i)->text().toUtf8().constData()); printf("}"); }
            else if (auto *v = qobject_cast<QLabel *>(w); v && v->frameShape() == QFrame::Panel) printf(" %u='%s'", id, v->text().toUtf8().constData());
            else if (!w->isEnabled() && qobject_cast<QPushButton *>(w)) printf(" %u(g)", id);
        }
    }
    printf("\n");
}
