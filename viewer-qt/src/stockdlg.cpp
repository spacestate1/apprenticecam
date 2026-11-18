/* Machine Group Properties (core_ui cui_stock.c / cui_grpprops.c, the models and their citations). The property sheet's tabs:
   opmanres 3852 Files / 3853 Tool Settings / 3855 Stock Setup (a mill group has no 4th page: core_ui ST_TABS); OK / Cancel
   (Apply / Help: NOT DECODED, absent). Each page's controls at their template positions (815 / 1589 / 1590, dialog units as
   the other template dialogs, chainDu). The sheet is as large as its largest page (815: 339 x 366 DLU). Stock Setup:
   the picture (opmanres bitmap 832 + FUN_1801f2980's index) under the controls it holds; the corner buttons 1294-1302 are
   hit rectangles over it (FUN_1801f0070) and the selected one gets FUN_1801ef4e0's marker. */
#include "stockdlg.h"
#include "icons.h"
#include "bpdlg.h"
#include "cp1252.h"
#include <QTextCursor>
extern "C" {
#include "core_ui/core_ui.h"
}
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QFrame>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QTabBar>
#include <cstdio>

namespace {
class Marker : public QWidget {                           /* FUN_1801ef4e0 drawn over the picture */
public:
    explicit Marker(QWidget *p) : QWidget(p) { setAttribute(Qt::WA_TransparentForMouseEvents); }
    QRect corner;                                         /* the selected corner button, in this widget's coordinates */
    bool on = false;
protected:
    void paintEvent(QPaintEvent *) override {
        if (!on) return;
        QPainter g(this); g.setPen(QPen(Qt::black, 1)); g.setBrush(Qt::black);
        int cx = corner.center().x(), cy = corner.center().y();
        g.drawLine(0, height() * 3 / 4, cx - 14, cy);
        QPolygon tri; tri << QPoint(cx - 6, cy) << QPoint(cx - 14, cy + 4) << QPoint(cx - 14, cy - 4); g.drawPolygon(tri);
        g.drawLine(cx - 4, cy, cx + 4, cy); g.drawLine(cx, cy - 4, cx, cy + 4);
    }
};
class CornerHit : public QWidget {                        /* a corner button's rectangle (clicks only) */
public:
    CornerHit(QWidget *p, unsigned id, std::function<void(unsigned)> f) : QWidget(p), id_(id), f_(std::move(f)) { setCursor(Qt::ArrowCursor); }
protected:
    void mousePressEvent(QMouseEvent *e) override { if (e->button() == Qt::LeftButton && isEnabled()) f_(id_); }
private:
    unsigned id_; std::function<void(unsigned)> f_;
};
QString noAmp(const char *t) { QString s = QString::fromUtf8(t); s.remove('&'); return s; }
/* the pages' texts: Files / Tool Settings hold the group's ANSI strings (cp1252.h), Stock Setup this program's UTF-8 paths */
QString txtIn(int pg, const char *t) { return !t ? QString() : pg == 2 ? QString::fromUtf8(t) : Cp1252::toQ(t); }
QByteArray txtOut(int pg, const QString &q) { return pg == 2 ? q.toUtf8() : Cp1252::fromQ(q); }
/* EM_LIMITTEXT (DDV_MaxChars 1999 of the comment, FUN_1801e3940): ANSI characters, a line end = CR LF = 2 */
int limLen(const QString &t) { return (int)Cp1252::fromQ(t).size() + (int)t.count('\n'); }
}

StockSetupDialog::StockSetupDialog(QWidget *parent) : FloatDlg(ST_SHEET_TITLE, parent) {
    setObjectName("stocksetup");
    QRect pr = chainDu(this, 0, 0, 339, 366);              /* 815 339 x 366, 1589 338 x 366, 1590 338 x 366 */
    const int tabh = 26, btnh = 40;
    setBodySize(QSize(pr.width() + 16, pr.height() + tabh + btnh + 8));
    QWidget *b = body();
    tabs_ = new QTabBar(b); tabs_->setGeometry(8, 4, pr.width(), tabh - 2); tabs_->setDrawBase(true);
    for (int k = 0; k < 3; k++) tabs_->addTab(ST_TABS[k]);
    tabs_->setCurrentIndex(2);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int k) { if (filling_) return; st_set_page(k); refresh(); emit changed(); });
    for (int k = 0; k < 3; k++) { pages_[k] = new QWidget(b); pages_[k]->setGeometry(8, tabh + 4, pr.width(), pr.height()); buildPage(k); }
    ok_ = new QPushButton("OK", b); cancel_ = new QPushButton("Cancel", b);
    int by = tabh + 4 + pr.height() + 6;
    cancel_->setGeometry(8 + pr.width() - 90, by, 90, 26); ok_->setGeometry(8 + pr.width() - 186, by, 90, 26);
    connect(ok_, &QPushButton::clicked, this, [this] { st_ok(); emit changed(); });
    connect(cancel_, &QPushButton::clicked, this, [this] { st_cancel(); emit changed(); });
    connect(this, &FloatDlg::closeClicked, this, [this] { st_cancel(); emit changed(); });
    bp_widget_font(this);
}

void StockSetupDialog::buildPage(int pg) {
    QWidget *page = pages_[pg]; QMap<unsigned, QWidget *> &wm = w_[pg];
    auto act = [this, pg](unsigned id) { sh_click(pg, id); refresh(); emit changed(); };
    /* the picture first: the controls it holds lie above it */
    for (const StCtl *c = sh_table(pg); c->text; c++) if (c->role == STR_PICTURE) {
        QRect r = chainDu(this, c->x, c->y, c->w, c->h);
        pic_ = new QLabel(page); pic_->setGeometry(r); pic_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        marker_ = new Marker(page); marker_->setGeometry(r);
    }
    for (const StCtl *c = sh_table(pg); c->text; c++) {
        QRect r = chainDu(this, c->x, c->y, c->w, c->h); unsigned id = c->id; QWidget *w = nullptr;
        switch (c->role) {
        case STR_GROUP: { auto *g = new QGroupBox(noAmp(c->text), page); g->setGeometry(r); g->lower(); w = g; } break;
        case STR_STATIC: { auto *l = new QLabel(noAmp(c->text), page); l->setGeometry(r.adjusted(0, -2, 10, 2)); w = l; } break;
        case STR_LABEL: { auto *l = new QLabel(page); l->setGeometry(r.adjusted(0, -2, 10, 2));   /* a static showing a value (UILabel / CString) */
            l->setAlignment(Qt::AlignLeft | Qt::AlignTop); l->setWordWrap(c->h > 8); w = l; } break;
        case STR_ETCH: { auto *f = new QFrame(page); f->setFrameShape(QFrame::HLine); f->setFrameShadow(QFrame::Sunken); f->setGeometry(r.x(), r.y(), r.width(), 2); w = f; } break;
        case STR_GROUPCHK: case STR_CHECK: { QString t = noAmp(c->text);
            if (c->h > 10) {                               /* a two-line check box (1291: 18 DLU, BS_MULTILINE) - ours: broken at the last
                                                              space that fits, as the button wraps its text */
                QFontMetrics fm(font()); int room = r.width() - 18, cut = -1;
                for (int i = 0; i < t.size(); i++) if (t[i] == ' ' && fm.horizontalAdvance(t.left(i)) <= room) cut = i;
                if (cut > 0 && fm.horizontalAdvance(t) > room) t[cut] = '\n';
            }
            auto *x = new QCheckBox(t, page); x->setGeometry(r.adjusted(0, -2, 14, 2));
            connect(x, &QCheckBox::clicked, this, [this, act, id] { if (!filling_) act(id); }); w = x; } break;
        case STR_RADIO: { auto *x = new QRadioButton(noAmp(c->text), page); x->setAutoExclusive(false); x->setGeometry(r.adjusted(0, -2, 14, 2));
            connect(x, &QRadioButton::clicked, this, [this, act, id] { if (!filling_) act(id); }); w = x; } break;
        case STR_BUTTON: { auto *p = new QPushButton(noAmp(c->text), page); p->setGeometry(r);
            connect(p, &QPushButton::clicked, this, [act, id] { act(id); }); w = p; } break;
        case STR_BMPBTN: { auto *p = new QPushButton(page); p->setGeometry(r);   /* UIBitmapButton (FUN_1801ef9d0 / FUN_1801e4410) */
            int bmp = sh_bitmap(pg, id);
            QPixmap pm = bmp ? Icons::image(QString("#file:opmanres_BMP_%1.png").arg(bmp)) : QPixmap();
            if (!pm.isNull()) { p->setIcon(QIcon(pm)); p->setIconSize(pm.size()); } else p->setText(noAmp(c->text));
            p->setToolTip(noAmp(c->text));
            connect(p, &QPushButton::clicked, this, [act, id] { act(id); }); w = p; } break;
        case STR_EDIT: { auto *e = new QLineEdit(page); e->setGeometry(r);
            connect(e, &QLineEdit::editingFinished, this, [this, e, id, pg] { if (filling_) return; sh_edit(pg, id, txtOut(pg, e->text()).constData()); refresh(); emit changed(); });   /* kill focus */
            w = e; } break;
        case STR_EDITML: { auto *e = new QPlainTextEdit(page); e->setGeometry(r);   /* a multi-line edit (ES_MULTILINE): the text at every change */
            connect(e, &QPlainTextEdit::textChanged, this, [this, e, id, pg] { if (filling_) return;
                if (limLen(e->toPlainText()) > 1999) {      /* the input past the limit is refused at the cursor (a paste: the part that fits) */
                    filling_ = true; QTextCursor tc = e->textCursor();
                    while (limLen(e->toPlainText()) > 1999 && tc.position() > 0) tc.deletePreviousChar();
                    e->setTextCursor(tc); filling_ = false;
                }
                sh_edit(pg, id, txtOut(pg, e->toPlainText()).constData()); emit changed(); });
            w = e; } break;
        case STR_COMBO: { auto *m = new QComboBox(page); m->setGeometry(r.x(), r.y(), r.width(), chainDu(this, 0, 0, 0, 14).height()); w = m; } break;
        case STR_CORNER: { auto *h = new CornerHit(page, id, act); h->setGeometry(r); w = h; } break;
        default: break;
        }
        if (!w) continue;
        w->show();
        if (id != 4294901759u) wm[id] = w;                 /* the templates' -1 statics / group boxes: no state */
    }
}

void StockSetupDialog::refresh() {
    filling_ = true;
    int cur = st_page >= 0 && st_page < 3 ? st_page : 2;
    tabs_->setCurrentIndex(cur);
    for (int k = 0; k < 3; k++) pages_[k]->setVisible(k == cur);
    if (pic_) { QPixmap pm = Icons::image(QString("#file:opmanres_BMP_%1.png").arg(st_picture_bmp()));
        if (pm.isNull()) pic_->setStyleSheet("border: 1px solid rgb(205,205,205);"); else pic_->setPixmap(pm); pic_->lower(); }
    for (int pg = 0; pg < 3; pg++)
        for (auto it = w_[pg].begin(); it != w_[pg].end(); ++it) {
            unsigned id = it.key(); QWidget *w = it.value();
            const StCtl *c = sh_ctl(pg, id); if (!c) continue;
            w->setVisible(sh_shown(pg, id)); w->setEnabled(sh_enabled(pg, id));
            if (auto *x = qobject_cast<QAbstractButton *>(w); x && (c->role == STR_CHECK || c->role == STR_GROUPCHK || c->role == STR_RADIO)) x->setChecked(sh_checked(pg, id));
            if (auto *e = qobject_cast<QLineEdit *>(w); e && !e->hasFocus()) { const char *t = sh_text(pg, id); e->setText(txtIn(pg, t)); }
            if (auto *e = qobject_cast<QPlainTextEdit *>(w); e && !e->hasFocus()) { const char *t = sh_text(pg, id); QString q = txtIn(pg, t); if (e->toPlainText() != q) e->setPlainText(q); }
            if (c->role == STR_LABEL) if (auto *l = qobject_cast<QLabel *>(w)) { const char *t = sh_text(pg, id); l->setText(txtIn(pg, t)); }
            if (auto *m = qobject_cast<QComboBox *>(w)) {     /* CFileComboBox: the file (its list NOT DECODED: the one file) */
                const char *t = sh_text(pg, id); QString f = t && t[0] ? QFileInfo(QString::fromUtf8(t)).fileName() : QString();
                if (m->count() != 1 || m->itemText(0) != f) { m->clear(); m->addItem(f); } }
        }
    if (auto *m = static_cast<Marker *>(marker_)) {
        int bt; m->on = st_marker(&bt) && w_[2].contains(bt);
        if (m->on) m->corner = w_[2][bt]->geometry().translated(-m->x(), -m->y());
        m->raise(); m->update();
    }
    for (auto it = w_[2].begin(); it != w_[2].end(); ++it) { const StCtl *c = st_ctl(it.key()); if (c && c->role == STR_CORNER) it.value()->raise(); }
    filling_ = false;
}

bool StockSetupDialog::testSet(const QString &key, const QString &v) {
    if (key == "ok") { ok_->click(); return true; }
    if (key == "cancel") { cancel_->click(); return true; }
    if (key == "tab") { int k = v.toInt(); if (k < 0 || k > 2) return false; tabs_->setCurrentIndex(k); return true; }   /* a tab clicked */
    int pg = st_page >= 0 && st_page < 3 ? st_page : 2;
    unsigned id = key.toUInt(); if (!w_[pg].contains(id) || !w_[pg][id]->isEnabled() || !w_[pg][id]->isVisible()) return false;
    QWidget *w = w_[pg][id];
    if (auto *e = qobject_cast<QLineEdit *>(w)) { e->setText(v); emit e->editingFinished(); return true; }
    if (auto *e = qobject_cast<QPlainTextEdit *>(w)) { QString t = v; t.replace("\\n", "\n"); e->setPlainText(t); refresh(); return true; }   /* \n = a new line (tests) */
    if (auto *x = qobject_cast<QAbstractButton *>(w)) { x->click(); return true; }
    const StCtl *c = sh_ctl(pg, id);
    if (c && c->role == STR_CORNER) { QMouseEvent pe(QEvent::MouseButtonPress, QPointF(2, 2), w->mapToGlobal(QPointF(2, 2)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(w, &pe); return true; }
    return false;
}

void StockSetupDialog::dump() const {
    printf("    stdlg:");
    int pg = st_page >= 0 && st_page < 3 ? st_page : 2;
    if (pg != 2) printf(" tab %d", pg);                     /* (the Stock Setup page's line as before) */
    for (auto it = w_[pg].begin(); it != w_[pg].end(); ++it) {
        unsigned id = it.key(); QWidget *w = it.value(); if (!w->isVisible()) continue;
        const StCtl *c = sh_ctl(pg, id);
        if (auto *x = qobject_cast<QAbstractButton *>(w); x && x->isCheckable() && x->isChecked()) printf(" [%u]", id);
        if (auto *e = qobject_cast<QLineEdit *>(w)) printf(" %u=%s%s", id, e->text().toUtf8().constData(), e->isEnabled() ? "" : "(g)");
        else if (auto *e = qobject_cast<QPlainTextEdit *>(w)) printf(" %u='%s'%s", id, e->toPlainText().replace("\n", "\\n").toUtf8().constData(), e->isEnabled() ? "" : "(g)");
        else if (c && c->role == STR_LABEL) printf(" %u='%s'", id, static_cast<QLabel *>(w)->text().toUtf8().constData());
        else if (!w->isEnabled()) printf(" %u(g)", id);
    }
    printf("\n");
}
