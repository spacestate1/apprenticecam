#include "retire.h"
#include "textfit.h"
#include "toolwiz.h"
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QFrame>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <cstdio>
#include <cstring>
extern "C" {
#include "toolprofile.h"
}

static const QColor HEAD(30, 50, 135), GREYT(150, 150, 150), DIS(160, 160, 160);   /* mcamui tw_group / greyed labels */
static void dlgFont(QWidget *w) { w->setFont(TextFit::dialogFont(w->font())); }   /* the dialogs' font (UI-4; was Noto Sans 12 px as mcamui) */
template <class T> static T *mk(QWidget *parent) { T *w = new T(parent); dlgFont(w); w->show(); return w; }
static QLabel *label(QWidget *p, const QString &t, const QColor &c = QColor()) {
    auto *l = mk<QLabel>(p); l->setText(t); if (c.isValid()) l->setStyleSheet(QString("color: rgb(%1,%2,%3);").arg(c.red()).arg(c.green()).arg(c.blue())); return l; }

/* the tool picture (2.5: ToolProfileGenerator output, toolprofile.c tool_outline) or, when ToolValidityChecker fails, the error
   panel - as mcamui tool_preview_rect: rgb(250,251,253) with a rgb(153,153,153) frame, the outline mirrored about the axis in
   rgb(20,80,170) 1.5 px, FitSpacing 15 */
class ToolPreview : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        QRectF b = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.fillRect(b, QColor(250, 251, 253)); p.setPen(QColor(153, 153, 153)); p.drawRect(b);
        const Tool *t = &tedit; const char *err = tw_validate(t);
        if (err) {
            p.setPen(QColor(200, 30, 30)); p.drawText(QPointF(12, 28), "!");
            p.setPen(QColor(40, 40, 40)); p.drawText(QRectF(30, 14, width() - 40, height() - 20), Qt::TextWordWrap, QString::fromUtf8(err));
            return;
        }
        double r[96], z[96]; int n = tool_outline(t, r, z, 96);
        double rmax = 1e-9, zmax = 1e-9; for (int i = 0; i < n; i++) { if (r[i] > rmax) rmax = r[i]; if (z[i] > zmax) zmax = z[i]; }
        double sc = fmin((width() - 30) / (2 * rmax), (height() - 30) / zmax);
        double cx = width() / 2.0, by = height() - 15;
        p.setPen(QPen(QColor(20, 80, 170), 1.5));
        for (int side = -1; side <= 1; side += 2)
            for (int i = 1; i < n; i++) p.drawLine(QPointF(cx + side * r[i - 1] * sc, by - z[i - 1] * sc), QPointF(cx + side * r[i] * sc, by - z[i] * sc));
    }
};

/* ------------------------------------------------------------------------------------------------ ToolWizardDialog */
ToolWizardDialog::ToolWizardDialog(QWidget *parent) : FloatDlg("Define Tool", parent) {
    setObjectName("toolWizard");
    setBodySize(QSize(200 + 810 + 24, 520 + 50));       /* mcamui: a 1040 x 620 window: the 200 px sidebar, the 810 px page, buttons */
    QWidget *b = body();
    side_ = mk<QFrame>(b); static_cast<QFrame *>(side_)->setFrameShape(QFrame::Box); side_->setGeometry(4, 4, 200, 520);
    cap_ = label(b, "", HEAD); cap_->setGeometry(214, 4, 810, 24);
    page_ = mk<QWidget>(b); page_->setGeometry(214, 30, 810, 494);
    static const char *bt[5] = { "Back", "Next", "Finish", "Cancel", "Help" };
    for (int k = 0; k < 5; k++) { btn_[k] = mk<QPushButton>(b); btn_[k]->setText(bt[k]); btn_[k]->setGeometry(4 + k * 84, 532, 80, 26); }
    btn_[4]->setEnabled(false);                        /* HelpBrowser topic 75005: not available */
    connect(btn_[0], &QPushButton::clicked, this, [this] { act([] { tw_back(); }); });
    connect(btn_[1], &QPushButton::clicked, this, [this] { act([] { tw_next(); }); });
    connect(btn_[2], &QPushButton::clicked, this, [this] { act([] { tw_finish(); }); });   /* Finish: CanFinish = !IsReadOnly; an invalid tool does not disable it */
    connect(btn_[3], &QPushButton::clicked, this, [this] { act([] { tedit_close(); }); });
    connect(this, &FloatDlg::closeClicked, this, [this] { act([] { tedit_close(); }); });
    /* ToolWizardWarningView (2.10): StdSizeOverwritingToolGeometry */
    warn_ = mk<QFrame>(b); warn_->setFrameShape(QFrame::Box); warn_->setAutoFillBackground(true); warn_->setGeometry(300, 200, 420, 150); warn_->hide();
    label(warn_, "Tool Manager")->setGeometry(10, 6, 400, 18);
    label(warn_, "Changing the standard size overwrites tool geometry settings.")->setGeometry(10, 30, 400, 20);
    label(warn_, "Do you wish to continue?")->setGeometry(10, 52, 400, 20);
    warnDont_ = mk<QCheckBox>(warn_); warnDont_->setText("Do not show this warning again for this session."); warnDont_->setGeometry(10, 76, 400, 20);
    auto *y = mk<QPushButton>(warn_); y->setText("Yes"); y->setGeometry(10, 112, 75, 23);
    auto *no = mk<QPushButton>(warn_); no->setText("No"); no->setGeometry(90, 112, 75, 23);
    connect(y, &QPushButton::clicked, this, [this] { bool d = warnDont_->isChecked(); act([d] { tw_warn_answer(1, d); }); });
    connect(no, &QPushButton::clicked, this, [this] { bool d = warnDont_->isChecked(); act([d] { tw_warn_answer(0, d); }); });
}
void ToolWizardDialog::act(const std::function<void()> &fn) { if (filling_) return; fn(); refresh(true); emit changed(); }
static QByteArray twSig() {
    QByteArray s; s.append((const char *)&tedit, sizeof tedit); s.append((const char *)&tws, sizeof tws);
    int v[7] = { tw_page, tedit_index, tw_warn_row, tw_nsizes, tw_size_pick, tm_lib_tool_edit, cool.open };
    s.append((const char *)v, sizeof v); return s;
}
void ToolWizardDialog::refresh(bool force) {
    if (tedit_index < -1) return;
    tedit.has_extras = 1;                              /* as mcamui draw_tool_editor */
    QByteArray s = twSig();
    if (!force && s == sig_) return;
    sig_ = s; filling_ = true;
    setTitle(QString::fromUtf8(tw_title()));
    char cap[96]; tw_caption(cap, sizeof cap); cap_->setText(QString::fromUtf8(cap));
    /* the step list «Current Step:» (edit mode: the type page is removed) */
    for (QWidget *w : side_->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) retireWidget(w);   /* a clicked one may be running its handler (retire.h) */
    label(side_, "Current Step:")->setGeometry(6, 4, 188, 26);
    for (int p = tw_first_page(), y = 32; p < 3; p++, y += 26) {
        auto *l = mk<QPushButton>(side_); l->setText(QString::fromUtf8(TW_TITLES[p])); l->setFlat(true); l->setCheckable(true); l->setChecked(tw_page == p);
        l->setStyleSheet("QPushButton { text-align: left; padding-left: 4px; } QPushButton:checked { background: rgb(51,153,255); color: white; }");
        l->setGeometry(4, y, 192, 24);
        connect(l, &QPushButton::clicked, this, [this, p] { act([p] { tw_goto(p); }); });
    }
    btn_[0]->setEnabled(tw_can_back()); btn_[1]->setEnabled(tw_can_next()); btn_[2]->setEnabled(tw_can_finish());
    buildPage();
    warn_->setVisible(tw_warn_row >= 0); if (tw_warn_row >= 0) warn_->raise();
    filling_ = false;
}
/* the rows of a page into a vertical list (mcamui's nuklear rows: label cell w - 100, a 10 px gap, the 80 px box) */
static QWidget *rowBox(QWidget *parent, QVBoxLayout *v, int h = 22) { auto *r = new QWidget(parent); r->setFixedHeight(h); v->addWidget(r); r->show(); return r; }
void ToolWizardDialog::buildPage() {
    for (QWidget *w : page_->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) retireWidget(w);
    ctls_.clear();
    Tool *t = &tedit;
    auto group = [](QWidget *p, QVBoxLayout *v, const char *text) { v->addSpacing(6); auto *l = label(p, QString::fromUtf8(text), HEAD); l->setFixedHeight(20); v->addWidget(l); };
    /* an edit box: commits on Enter / leaving the box (ToolEditBoxConverter 2.5: a plain number >= 0, else the old value stays) */
    auto numBox = [this](QWidget *p, const QString &shown, const QString &key, const std::function<void(double)> &set, bool isInt) {
        auto *e = mk<QLineEdit>(p); e->setText(shown);
        connect(e, &QLineEdit::editingFinished, this, [this, e, set, isInt] {
            if (!e->isModified()) return; e->setModified(false);
            QByteArray b = e->text().toUtf8(); double d; long l;
            if (isInt ? tw_parse_int(b.constData(), &l) : tw_parse_num(b.constData(), &d)) { double v = isInt ? (double)l : d; act([set, v] { set(v); }); }
            else refresh(true);
        });
        ctls_.append({ key, e }); return e;
    };
    auto cell = [](QWidget *row, QWidget *lab, QWidget *box, int w) { lab->setParent(row); lab->setGeometry(0, 0, w - 100, 22); lab->show(); if (box) { box->setParent(row); box->setGeometry(w - 90, 0, 80, 22); box->show(); } };
    if (tw_page == 0) {                                /* Select Tool Type (2.4: ToolTypePageView.GenerateModelList) */
        label(page_, "Select the tool type you wish to create from the list below.")->setGeometry(0, 0, 800, 20);
        auto *box = mk<QFrame>(page_); box->setFrameShape(QFrame::Box); box->setGeometry(0, 24, 800, 400);
        const TwTypeItem *it; int n = tw_type_items(&it); int y = 4;
        static const char *groups[2] = { "Milling", "Holemaking" };
        for (int g = 0; g < 2; g++) {
            label(box, groups[g], HEAD)->setGeometry(6, y, 300, 24); y += 26; int col = 0;
            for (int i = 0; i < n; i++) {
                if (it[i].group != g) continue;
                auto *b = mk<QPushButton>(box); b->setText(QString::fromUtf8(it[i].label)); b->setFlat(true); b->setCheckable(true); b->setChecked(t->type == it[i].type);
                b->setStyleSheet("QPushButton { text-align: left; padding-left: 4px; } QPushButton:checked { background: rgb(51,153,255); color: white; }");
                b->setGeometry(6 + col * 200, y, 196, 26); int ty = it[i].type;
                connect(b, &QPushButton::clicked, this, [this, ty] { act([ty] { tw_pick_type(ty); }); });   /* PreviewSelectedPageChanging */
                ctls_.append({ QString("type:%1").arg(ty), b });
                if (++col == 3) { col = 0; y += 30; }
            }
            if (col) y += 30;
        }
        return;
    }
    if (tw_page == 1) {                                /* Define Tool Geometry (2.5 / 2.6: toolwiz.c tw_rows) */
        label(page_, "Adjust geometric properties used to define the tool shape.")->setGeometry(0, 0, 800, 20);
        auto *sc = mk<QScrollArea>(page_); sc->setGeometry(0, 24, 370, 400); sc->setFrameShape(QFrame::NoFrame); sc->setWidgetResizable(true);
        auto *inner = new QWidget; auto *v = new QVBoxLayout(inner); v->setContentsMargins(0, 0, 4, 0); v->setSpacing(4);
        const TwRow *rows; int nr = tw_rows(t->type, &rows);
        int corner = tw_corner_index(t), upper = tw_slot_upper_index(t), shank = t->shank_type, tabsel = -1;
        auto tabs = [this, inner, v](const char *const *lab, int n, int cur, const int *en, const QString &key, const std::function<void(int)> &set) {
            auto *r = rowBox(inner, v, 24); int w = 340 / (n > 0 ? n : 1);
            for (int i = 0; i < n; i++) {
                auto *b = mk<QPushButton>(r); b->setText(QString::fromUtf8(lab[i])); b->setCheckable(true); b->setChecked(i == cur); b->setGeometry(i * w, 0, w - 2, 24);
                b->setEnabled(!en || en[i]); connect(b, &QPushButton::clicked, this, [this, i, set] { act([set, i] { set(i); }); });
                ctls_.append({ QString("%1:%2").arg(key).arg(i), b });
            }
        };
        for (int i = 0; i < nr; i++) {
            const TwRow *rw = &rows[i];
            switch (rw->kind) {
            case TWR_GROUP: group(inner, v, rw->label); break;
            case TWR_BOX: {
                if (rw->tab >= 0 && rw->tab != tabsel) break;   /* inside a tab that is not selected */
                auto *r = rowBox(inner, v); char shown[48]; tw_format(t, rw->field, shown, sizeof shown);
                QLabel *l = label(r, QString::fromUtf8(rw->field == TWF_THREAD_INFO ? tw_thread_info_label(t) : rw->label));
                QString key = QString("f:%1").arg(QString::fromUtf8(tw_field_name(rw->field)));
                if (!tw_box_enabled(t, rw->field)) { QLabel *g = label(r, QString::fromUtf8(shown), GREYT); cell(r, l, g, 345); ctls_.append({ key, g }); }
                else { int f = rw->field; cell(r, l, numBox(r, QString::fromUtf8(shown), key, [f](double d) { tw_set(&tedit, &tws, f, d); }, false), 345); }
                break; }
            case TWR_CORNER_TABS: { const char *const *lab; int n = tw_corner_items(t->type, &lab);
                tabs(lab, n, corner, nullptr, "corner", [](int k) { tw_set_corner_index(&tedit, &tws, k); }); tabsel = corner; break; }
            case TWR_CORNER_COMBO: { const char *const *lab; int n = tw_corner_items(t->type, &lab);
                auto *r = rowBox(inner, v); cell(r, label(r, QString::fromUtf8(rw->label)), nullptr, 345);
                auto *c = mk<QComboBox>(inner); for (int k = 0; k < n; k++) c->addItem(QString::fromUtf8(lab[k])); c->setCurrentIndex(corner); v->addWidget(c);
                connect(c, QOverload<int>::of(&QComboBox::activated), this, [this](int k) { act([k] { tw_set_corner_index(&tedit, &tws, k); }); });
                ctls_.append({ "corner", c }); tabsel = corner; break; }
            case TWR_SLOT_CORNERS: {
                auto *cb = mk<QCheckBox>(inner); cb->setText("Lock upper and lower corners"); cb->setChecked(tws.lock_corners); v->addWidget(cb);
                connect(cb, &QCheckBox::toggled, this, [this](bool on) { act([on] { tw_set_lock_corners(&tedit, &tws, on); }); });
                ctls_.append({ "lock", cb });
                const char *const *lab; int n = tw_corner_items(t->type, &lab);
                tabs(lab, n, upper, nullptr, "upper", [](int k) { tw_set_slot_upper_index(&tedit, &tws, k); }); tabsel = upper; break; }
            case TWR_THREAD_FORM: { const char *const *shapes; int nsh = tw_thread_form_items(&shapes);
                auto *r = rowBox(inner, v); cell(r, label(r, QString::fromUtf8(rw->label)), nullptr, 345);
                auto *c = mk<QComboBox>(inner); for (int k = 0; k < nsh; k++) c->addItem(QString::fromUtf8(shapes[k])); c->setCurrentIndex(t->thread_shape >= 0 && t->thread_shape < nsh ? t->thread_shape : 0); v->addWidget(c);
                connect(c, QOverload<int>::of(&QComboBox::activated), this, [this](int k) { act([k] { if (k != tedit.thread_shape) tw_set_thread_form(&tedit, &tws, k); }); });
                ctls_.append({ "thread", c }); break; }
            case TWR_LEFT_HAND: { auto *cb = mk<QCheckBox>(inner); cb->setText("Left hand"); cb->setChecked(t->type == 5); v->addWidget(cb);
                connect(cb, &QCheckBox::toggled, this, [this](bool on) { act([on] { tw_set_left_hand(&tedit, &tws, on); }); });
                ctls_.append({ "lefthand", cb }); break; }
            case TWR_TAP_TIP: { auto *c = mk<QComboBox>(inner); c->addItems({ "Plug", "Bottoming" }); c->setCurrentIndex(tw_tap_tip_index(t)); v->addWidget(c);
                connect(c, QOverload<int>::of(&QComboBox::activated), this, [this](int k) { act([k] { if (k != tw_tap_tip_index(&tedit)) tw_set_tap_tip_index(&tedit, &tws, k); }); });
                ctls_.append({ "taptip", c }); break; }
            case TWR_SHANK_TABS: { static const char *lab[3] = { "Straight Shank", "Tapered Shank", "Reduced Neck Shank" };
                int en[3] = { 1, tw_shank_possible(t, 1), tw_shank_possible(t, 2) };
                tabs(lab, 3, shank, en, "shank", [](int k) { tw_set_shank_style(&tedit, &tws, k); }); tabsel = shank; break; }
            case TWR_STD_SIZES: {                       /* «Standard sizes» (DropdownGridView 2.6.y: core_ui tw_std_*) */
                tw_std_refresh(t);
                if (tw_nsizes <= 0) { auto *l = label(inner, "(no catalog: DatabaseCatalog.tooldb not found)", GREYT); l->setFixedHeight(22); v->addWidget(l); break; }
                auto *c = mk<QComboBox>(inner); c->addItem(QString::fromUtf8(tw_std_prompt(t)));
                /* the popup scrolls after 15 rows (ours, the user's request: the DropdownGridView's height is NOT DECODED);
                   combobox-popup 0 = a list popup, so styles that show every row honour maxVisibleItems */
                c->setStyleSheet("QComboBox { combobox-popup: 0; }"); c->setMaxVisibleItems(15);
                for (int k = 0; k < tw_nsizes; k++) { char row[160]; tw_std_row_text(t, k, row, sizeof row); c->addItem(QString::fromUtf8(row)); }
                v->addWidget(c);
                connect(c, QOverload<int>::of(&QComboBox::activated), this, [this](int k) { if (k >= 1) act([k] { tw_std_choose(k - 1); }); });
                ctls_.append({ "std", c }); break; }
            }
        }
        v->addStretch(1);
        sc->setWidget(inner);
        auto *pv = new ToolPreview(page_); pv->setGeometry(390, 24, 400, 390); pv->show();
        return;
    }
    /* Finalize Properties (2.9: core_ui tw_prop_rows, the Operation column 400 px and General + the type panel 350 px) */
    label(page_, "Adjust any miscellaneous properties before finalizing tool creation.")->setGeometry(0, 0, 800, 20);
    QScrollArea *sc[2]; QVBoxLayout *v[2]; QWidget *in[2];
    for (int k = 0; k < 2; k++) {
        sc[k] = mk<QScrollArea>(page_); sc[k]->setGeometry(k ? 420 : 0, 24, k ? 350 : 400, 400); sc[k]->setFrameShape(QFrame::NoFrame); sc[k]->setWidgetResizable(true);
        in[k] = new QWidget; v[k] = new QVBoxLayout(in[k]); v[k]->setContentsMargins(0, 0, 4, 0); v[k]->setSpacing(4);
    }
    TwPRow pr[64]; int n = tw_prop_rows(t, pr, 64);
    for (int k = 0; k < n; k++) {
        const TwPRow &r = pr[k]; int c = r.col, w = c ? 310 : 360; QWidget *inn = in[c]; QVBoxLayout *vv = v[c];
        QString key = QString("p:%1").arg(r.id); int id = r.id;
        switch (r.kind) {
        case TWP_GROUP: group(inn, vv, r.label); break;
        case TWP_INT: case TWP_NUM: { auto *row = rowBox(inn, vv); char b[48]; tw_prop_text(t, id, r.kind == TWP_INT, b, sizeof b);
            cell(row, label(row, QString::fromUtf8(r.label)), numBox(row, QString::fromUtf8(b), key, [id](double d) { tw_prop_set(&tedit, id, d); }, r.kind == TWP_INT), w); break; }
        case TWP_COMBO: { const char *const *items; int cur, m = tw_prop_combo(t, id, &items, &cur);
            auto *row = rowBox(inn, vv); cell(row, label(row, QString::fromUtf8(r.label)), nullptr, w);
            if (m <= 0 && id == 131) { auto *l = label(inn, t->mat_name[0] ? QString::fromUtf8(t->mat_name) : QString("(DatabaseCatalog.tooldb not found)")); vv->addWidget(l); break; }
            auto *cb = mk<QComboBox>(inn); for (int q = 0; q < m; q++) cb->addItem(QString::fromUtf8(items[q])); cb->setCurrentIndex(cur < 0 ? 0 : cur); vv->addWidget(cb);
            connect(cb, QOverload<int>::of(&QComboBox::activated), this, [this, id](int q) { act([id, q] { tw_prop_set_combo(&tedit, id, q); }); });
            ctls_.append({ key, cb }); break; }
        case TWP_TEXT: { auto *row = rowBox(inn, vv); auto *l = label(row, QString::fromUtf8(r.label)); l->setGeometry(0, 0, 140, 22);
            size_t cap; char *s = tw_prop_str(t, id, &cap); auto *e = mk<QLineEdit>(row); e->setText(QString::fromUtf8(s)); e->setGeometry(144, 0, 180, 22); e->setMaxLength((int)cap - 1);
            connect(e, &QLineEdit::editingFinished, this, [this, e, id] { if (!e->isModified()) return; e->setModified(false); QByteArray b = e->text().toUtf8(); act([b, id] { tw_prop_set_str(&tedit, id, b.constData()); }); });
            ctls_.append({ key, e }); break; }
        case TWP_BUTTON: { auto *row = rowBox(inn, vv, 23); auto *b = mk<QPushButton>(row); b->setText(QString::fromUtf8(r.label)); b->setGeometry(0, 0, 75, 23);
            if (r.tip) b->setToolTip(QString::fromUtf8(r.tip));
            connect(b, &QPushButton::clicked, this, [this] { act([] { coolant_dlg_open(&tedit); }); });   /* EditCoolantCommand */
            ctls_.append({ key, b }); break; }
        case TWP_CHECK: {
            if (id == 136) break;                      /* with Rough Tool on one row */
            auto *row = rowBox(inn, vv);
            for (int q = 0; q < (id == 135 ? 2 : 1); q++) {
                int cid = id + q; auto *cb = mk<QCheckBox>(row); cb->setText(QString::fromUtf8(q ? "Finish Tool" : r.label)); cb->setChecked(tw_prop_check(t, cid)); cb->setGeometry(q * (w / 2), 0, w / 2, 22);
                if (r.tip) cb->setToolTip(QString::fromUtf8(r.tip));
                connect(cb, &QCheckBox::toggled, this, [this, cid](bool on) { act([cid, on] { tw_prop_set_check(&tedit, cid, on); }); });
                ctls_.append({ QString("p:%1").arg(cid), cb });
            }
            break; }
        }
    }
    for (int k = 0; k < 2; k++) { v[k]->addStretch(1); sc[k]->setWidget(in[k]); }
}
void ToolWizardDialog::dump() const {
    printf("    twdlg \"%s\" caption \"%s\" back %d next %d finish %d warn %d\n", windowTitle().toUtf8().constData(), cap_->text().toUtf8().constData(),
           btn_[0]->isEnabled(), btn_[1]->isEnabled(), btn_[2]->isEnabled(), warn_->isVisible());
    for (const auto &c : ctls_) {
        QString v;
        if (auto *le = qobject_cast<QLineEdit *>(c.w)) v = le->text();
        else if (auto *b = qobject_cast<QAbstractButton *>(c.w)) v = b->isCheckable() ? QString::number(b->isChecked()) : b->text();
        else if (auto *co = qobject_cast<QComboBox *>(c.w)) v = co->currentText();
        else if (auto *l = qobject_cast<QLabel *>(c.w)) v = l->text() + " (grey)";
        printf("      ctl %s = %s%s\n", c.key.toUtf8().constData(), v.toUtf8().constData(), c.w->isEnabled() ? "" : " (off)");
    }
}
bool ToolWizardDialog::testSet(const QString &key, const QString &v) {
    static const char *bt[5] = { "back", "next", "finish", "cancel", "help" };
    for (int k = 0; k < 5; k++) if (key == bt[k]) { if (!btn_[k]->isEnabled()) return false; btn_[k]->click(); return true; }
    if (key == "warn") { if (!warn_->isVisible()) return false; if (v.startsWith('d')) warnDont_->setChecked(true);
        for (auto *b : warn_->findChildren<QPushButton *>()) if (b->text() == (v.endsWith('Y') ? "Yes" : "No")) { b->click(); return true; } return false; }
    if (key == "step") { int p = v.toInt(); for (auto *b : side_->findChildren<QPushButton *>()) if (b->text() == QString::fromUtf8(TW_TITLES[p < 0 || p > 2 ? 0 : p])) { b->click(); return true; } return false; }
    for (const auto &c : ctls_) if (c.key == key && c.w->isEnabled()) {
        if (auto *le = qobject_cast<QLineEdit *>(c.w)) { le->setText(v); le->setModified(true); emit le->editingFinished(); return true; }
        if (auto *co = qobject_cast<QComboBox *>(c.w)) { co->setCurrentIndex(v.toInt()); emit co->activated(v.toInt()); return true; }
        if (auto *b = qobject_cast<QAbstractButton *>(c.w)) { if (qobject_cast<QCheckBox *>(b)) b->setChecked(v.toInt() != 0); else b->click(); return true; }
    }
    return false;
}

/* ------------------------------------------------------------------------------------------------ CoolantDialog */
/* CoolantView (2.12a; model core_ui cool_*): "Standard Coolant" Flood / Mist / Thru-tool On / Off combos (one On at a time:
   OldStyleFloodEnabled ...), "Custom Coolant" rows greyed (not kept by this program's tool record), Help greyed, OK, Cancel;
   rows 25 high 5 apart, columns 20 / 170 / 120 / 20 / 120 as mcamui draw_coolant_dialog */
CoolantDialog::CoolantDialog(QWidget *parent) : FloatDlg("Coolant", parent) {
    setClosable(false); setObjectName("coolant");
    int w = 20 + 150 + 20 + 120 + 20 + 120 + 20 + 30, h = 2 * 31 + 13 * 30 + 15 + 28 + 24;
    setBodySize(QSize(w, h));
    QWidget *b = body(); int y = 6;
    auto head = [b, w](int y, const char *t) { auto *l = label(b, t); l->setGeometry(8, y, 200, 26); auto *ln = mk<QFrame>(b); ln->setFrameShape(QFrame::HLine);
        ln->setGeometry(8 + l->fontMetrics().horizontalAdvance(t) + 8, y + 13, w - 24 - l->fontMetrics().horizontalAdvance(t), 1); ln->setStyleSheet("color: rgb(102,102,102);"); };
    head(y, "Standard Coolant"); y += 31;
    for (int i = 0; i < 3; i++, y += 30) {
        label(b, COOL_NAMES[i])->setGeometry(28, y, 170, 25);
        std_[i] = mk<QComboBox>(b); std_[i]->addItem(COOL_ONOFF[0]); std_[i]->addItem(COOL_ONOFF[1]); std_[i]->setGeometry(198, y, 120, 22);
        connect(std_[i], QOverload<int>::of(&QComboBox::activated), this, [this, i](int k) { if (!filling_) { cool_set(i, k); refresh(); } });
    }
    y += 10; head(y, "Custom Coolant"); y += 31;
    for (int i = 0; i < 10; i++, y += 30) {
        label(b, COOL_CNAMES[i])->setGeometry(28, y, 170, 25);
        for (int q = 0; q < 2; q++) { auto *c = mk<QComboBox>(b); c->setGeometry(198 + q * 140, y, 120, 22); c->setEnabled(false); }   /* NOT DONE (greyed) */
    }
    auto *help = mk<QPushButton>(b); help->setText("Help"); help->setGeometry(8, y + 4, 72, 23); help->setEnabled(false);
    ok_ = mk<QPushButton>(b); ok_->setText("OK"); ok_->setGeometry(w - 72 * 2 - 16, y + 4, 72, 23);
    cancel_ = mk<QPushButton>(b); cancel_->setText("Cancel"); cancel_->setGeometry(w - 72 - 8, y + 4, 72, 23);
    connect(ok_, &QPushButton::clicked, this, [this] { cool_ok(); emit closed(); });
    connect(cancel_, &QPushButton::clicked, this, [this] { cool_cancel(); emit closed(); });   /* CoolantView Cancel: IsCancel = True */
}
void CoolantDialog::refresh() {
    filling_ = true;
    for (int i = 0; i < 3; i++) { std_[i]->setCurrentIndex(cool_idx(i)); std_[i]->setEnabled(cool_enabled(i)); }
    filling_ = false;
}
void CoolantDialog::dump() const {
    printf("    cooldlg");
    for (int i = 0; i < 3; i++) printf(" %s=%s%s", COOL_NAMES[i], std_[i]->currentText().toUtf8().constData(), std_[i]->isEnabled() ? "" : "(grey)");
    printf(" style %d\n", cool.style);
}
bool CoolantDialog::testSet(const QString &key, const QString &v) {
    if (key == "ok") { ok_->click(); return true; }
    if (key == "cancel") { cancel_->click(); return true; }
    int i = key.toInt(); if (i < 0 || i > 2 || !std_[i]->isEnabled()) return false;
    std_[i]->setCurrentIndex(v.toInt()); emit std_[i]->activated(v.toInt()); return true;
}
