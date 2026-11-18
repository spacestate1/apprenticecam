#include "retire.h"
#include "geopanel.h"
#include "icons.h"
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QRadioButton>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QStandardItemModel>
#include <QScrollArea>
#include <QTabBar>
#include <QHash>
#include <cmath>
#include <cstdio>
#include <QAbstractButton>
#include <QFocusEvent>
#include <QCoreApplication>
#include "core_ui/core_ui.h"

/* the controls of one walk, in order (CuiPanelOps calls) */
enum { IT_EXP, IT_LABEL, IT_RADIO, IT_CHECK, IT_TOGGLE, IT_NUM, IT_INUM, IT_COMBO, IT_TEXT, IT_TBTN };
struct GeoPanel::Item {
    int kind = 0, val = 0;
    double lo = 0, hi = 0; int ilo = 0, ihi = 0;
    QWidget *w = nullptr; QCheckBox *lock = nullptr;
    QToolButton *hdr = nullptr; QWidget *box = nullptr; int exp_ix = 0;
    bool soft = false;                                 /* the field's soft lock (GEO_LK_SOFT), for the tests' dump */
};

/* the EvaluatorBox soft lock: a box with a lock (HasLock) that gets the keyboard focus is soft-locked
   (cncsoftware.controls.dll ValueLockBehavior.OnGotKeyboardFocus -> SetIsSoftLocked(true), attached by evaluatorbox.baml
   Setter_20 / _21; no panel code is posted for it). Written through the walk as lock value 2; core_ui keeps it only for
   the commands whose clearing of it is ported (geo_soft_lock_cmd) */
struct SoftLockFocus : QObject {
    GeoPanel *p; int k;
    SoftLockFocus(QObject *parent, GeoPanel *p_, int k_) : QObject(parent), p(p_), k(k_) {}
    bool eventFilter(QObject *o, QEvent *e) override { if (e->type() == QEvent::FocusIn && geo_soft_lock_cmd(geo_cmd)) p->edited(k, 0, 0, 2); return QObject::eventFilter(o, e); }
};
/* a lock value written through the walk: 0 / 1 the lock button (the hard lock), 2 the focus (the soft lock) */
static void lockWrite(int *lk, int v) {
    if (!lk) return;
    if (v == 2) { if (geo_soft_lock_cmd(geo_cmd)) *lk |= GEO_LK_SOFT; }
    else { *lk = (*lk & ~GEO_LK_HARD) | (v ? GEO_LK_HARD : 0); geo_lock_button(lk); }   /* the command's own lock-button handling */
}

/* access-key underscores removed ("Radi_us:" -> "Radius:"), as mcamui's fp_txt; the WPF access keys themselves are
   not bound here */
static QString fpTxt(const char *s) { QString t = QString::fromUtf8(s); t.remove('_'); return t; }
/* mcamui's number display: an enabled field shows nuklear's property text (2 decimals: vendor/nuklear.h
   NK_MAX_FLOAT_PRECISION, which wins over mcamui.c's own define), a disabled one "%.4f" (geopanel_ui.inc fp_num); the
   EvaluatorBox's own format is NOT DECODED */
static QString numTxt(double v, bool en) { return QString::asprintf(en ? "%.2f" : "%.4f", v); }

/* expander open / closed per command and expander index (mcamui fp_open: open by default) */
static QHash<int, bool> g_closed;

static GeoPanel *P(void *u) { return static_cast<GeoPanel *>(u); }
static QVBoxLayout *cur(GeoPanel *p) { return p->stack_.last(); }

static int op_exp(void *u, const char *h) {
    GeoPanel *p = P(u); int k = p->k_++;
    static int exp_counter; if (k == 0) exp_counter = 0;
    if (p->mode_ == 0) {
        auto *it = new GeoPanel::Item; it->kind = IT_EXP; it->exp_ix = exp_counter;
        it->hdr = new QToolButton; it->hdr->setText(fpTxt(h)); it->hdr->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        it->hdr->setAutoRaise(true); it->hdr->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QFont f = it->hdr->font(); f.setBold(true); it->hdr->setFont(f);
        it->box = new QWidget; auto *bl = new QVBoxLayout(it->box); bl->setContentsMargins(14, 0, 2, 4); bl->setSpacing(2);
        int key = geo_cmd * 64 + it->exp_ix; bool closed = g_closed.value(key, false);
        it->hdr->setArrowType(closed ? Qt::RightArrow : Qt::DownArrow); it->box->setVisible(!closed);
        QToolButton *hb = it->hdr; QWidget *bx = it->box;
        QObject::connect(hb, &QToolButton::clicked, hb, [hb, bx, key] {
            bool c = !g_closed.value(key, false); g_closed[key] = c; hb->setArrowType(c ? Qt::RightArrow : Qt::DownArrow); bx->setVisible(!c); });
        cur(p)->addWidget(it->hdr); cur(p)->addWidget(it->box);
        p->items_.append(it);
        p->stack_.append(bl);
    }
    exp_counter++;
    return 1;                                            /* the contents are always walked (a closed expander hides them) */
}
static void op_end(void *u) { GeoPanel *p = P(u); if (p->mode_ == 0 && p->stack_.size() > 1) p->stack_.removeLast(); }
/* "#img:NAME": a picture from the icon pack (NAME.png, tools/extract_icons.py), the resource name when the pack lacks it. "#cell:r,c": an untexted radio of a grid (Rectangular Shapes' Anchor group, res 6414) */
static bool isImg(const char *t) { return t && !strncmp(t, "#img:", 5); }
static bool isCell(const char *t) { return t && !strncmp(t, "#cell:", 6); }
static void op_label(void *u, const char *t) {
    GeoPanel *p = P(u); int k = p->k_++;
    if (p->mode_ == 0) { auto *it = new GeoPanel::Item; it->kind = IT_LABEL; it->w = new QLabel(isImg(t) ? QString() : fpTxt(t)); cur(p)->addWidget(it->w); p->items_.append(it); }
    if (isImg(t) && k < p->items_.size()) { QPixmap x = Icons::image("#file:" + QString::fromUtf8(t + 5) + ".png"); auto *l = static_cast<QLabel *>(p->items_[k]->w); if (!x.isNull()) l->setPixmap(x); else l->setText(QString::fromUtf8(t + 5)); }
}
static int op_radio(void *u, const char *t, int *var, int val, int en) {
    GeoPanel *p = P(u); int k = p->k_++, ret = 0;
    if (p->mode_ == 0) {
        auto *it = new GeoPanel::Item; it->kind = IT_RADIO; it->val = val;
        auto *rb = new QRadioButton(isImg(t) || isCell(t) ? QString() : fpTxt(t)); rb->setAutoExclusive(false);   /* the group is the model's variable */
        QObject::connect(rb, &QRadioButton::toggled, rb, [p, k, val](bool on) { if (on) p->edited(k, 0, val, -1); else p->edited(-1, 0, 0, -1); });
        it->w = rb;
        if (isCell(t)) { static QGridLayout *grid; int r = 0, c = 0; sscanf(t + 6, "%d,%d", &r, &c);
            if (r == 0 && c == 0) { auto *g = new QWidget; grid = new QGridLayout(g); grid->setContentsMargins(0, 0, 0, 0); cur(p)->addWidget(g); }
            grid->addWidget(rb, r, c); }
        else cur(p)->addWidget(rb);
        p->items_.append(it);
    } else if (p->mode_ == 1 && p->pend_.k == k && en && *var != val) { *var = val; ret = 1; }
    if (k < p->items_.size()) { auto *rb = static_cast<QRadioButton *>(p->items_[k]->w); QSignalBlocker b(rb); rb->setChecked(*var == val); rb->setEnabled(en != 0);
        if (isImg(t)) { QPixmap x = Icons::image("#file:" + QString::fromUtf8(t + 5) + ".png"); if (!x.isNull()) { rb->setIcon(QIcon(x)); rb->setIconSize(x.size()); } else rb->setText(QString::fromUtf8(t + 5)); } }
    return ret;
}
static void op_check(void *u, const char *t, int *var, int en) {
    GeoPanel *p = P(u); int k = p->k_++;
    if (p->mode_ == 0) {
        auto *it = new GeoPanel::Item; it->kind = IT_CHECK;
        auto *cb = new QCheckBox(fpTxt(t));
        QObject::connect(cb, &QCheckBox::toggled, cb, [p, k](bool on) { p->edited(k, 0, on ? 1 : 0, -1); });
        it->w = cb; cur(p)->addWidget(cb); p->items_.append(it);
    } else if (p->mode_ == 1 && p->pend_.k == k && en) *var = p->pend_.i;
    if (k < p->items_.size()) { auto *cb = static_cast<QCheckBox *>(p->items_[k]->w); QSignalBlocker b(cb); cb->setChecked(*var != 0); cb->setEnabled(en != 0); }
}
/* Reselect / 1 / 2 / 3 ToggleButtons (mcamui fp_toggle): code < 0 = not ported (greyed); code >= 0 = a live button that
   posts that panel code (the chain commands' Reselect, 4: GeoPanel::button -> core_ui geo_panel_button) */
static void op_toggle(void *u, const char *t, int n, int code) {
    GeoPanel *p = P(u); p->k_++;
    if (p->mode_ != 0) return;
    auto *it = new GeoPanel::Item; it->kind = IT_TOGGLE; it->val = code;
    QWidget *row = new QWidget; auto *hl = new QHBoxLayout(row); hl->setContentsMargins(0, 0, 0, 0); hl->setSpacing(4);
    for (int i = 0; i < n; i++) {
        auto *b = new QPushButton(n == 1 ? fpTxt(t) : QString::number(i + 1));
        if (code >= 0) QObject::connect(b, &QPushButton::clicked, p, [p, code] { emit p->button(code); });
        else { b->setCheckable(true); b->setEnabled(false); }
        int bw = 60;                                    /* nk_layout_row_static(22, 60) */
        if (n == 1) { int tw = b->fontMetrics().horizontalAdvance(b->text()) + 12; if (tw > bw) bw = tw; }   /* ours: a longer label (the Transform dialogs' buttons) is not cut */
        b->setFixedSize(bw, 22); hl->addWidget(b);
    }
    hl->addStretch(1);
    it->w = row; cur(p)->addWidget(row); p->items_.append(it);
}
/* ToggleButtons bound to *var (CuiPanelOps.tbtn): button i checked when *var == i; a click sets i, or none when it is checked
   (OnSetCurrentlyEditingCommand); one row, 60 x 22 as op_toggle's; one walk item (the value = the button clicked) */
static void op_tbtn(void *u, const char *t, int n, int *var, int none, int en) {
    GeoPanel *p = P(u); int k = p->k_++;
    if (p->mode_ == 0) {
        auto *it = new GeoPanel::Item; it->kind = IT_TBTN; it->val = n;
        QWidget *row = new QWidget; auto *hl = new QHBoxLayout(row); hl->setContentsMargins(0, 0, 0, 0); hl->setSpacing(4);
        QStringList tl = QString::fromUtf8(t).split('|');
        for (int i = 0; i < n; i++) {
            auto *b = new QPushButton(i < tl.size() ? fpTxt(tl[i].toUtf8().constData()) : QString::number(i + 1)); b->setCheckable(true); b->setFixedSize(60, 22);
            QObject::connect(b, &QPushButton::clicked, b, [p, k, i] { p->edited(k, 0, i, -1); });
            hl->addWidget(b);
        }
        hl->addStretch(1);
        it->w = row; cur(p)->addWidget(row); p->items_.append(it);
    } else if (p->mode_ == 1 && p->pend_.k == k && (en >> p->pend_.i & 1)) *var = *var == p->pend_.i ? none : p->pend_.i;
    if (k < p->items_.size()) {
        auto bl = p->items_[k]->w->findChildren<QPushButton *>();
        for (int i = 0; i < bl.size(); i++) { QSignalBlocker sb(bl[i]); bl[i]->setChecked(*var == i); bl[i]->setEnabled((en >> i & 1) != 0); }
    }
}
/* EvaluatorBox: optional label on its own row, the value, the lock (HasLock) */
static void numRow(GeoPanel *p, int k, GeoPanel::Item *it, const char *label, bool lk, bool isint) {
    if (label) { auto *l = new QLabel(fpTxt(label)); cur(p)->addWidget(l); }
    QWidget *row = new QWidget; auto *hl = new QHBoxLayout(row); hl->setContentsMargins(0, 0, 0, 0); hl->setSpacing(4);
    auto *le = new QLineEdit; hl->addWidget(le, 84);
    QObject::connect(le, &QLineEdit::editingFinished, le, [p, k, le, it, isint] {
        if (!le->isModified()) return;                  /* focus left without typing: the shown (rounded) text is not a value */
        le->setModified(false);
        bool ok; double v = le->text().toDouble(&ok);
        if (!ok) { p->edited(-1, 0, 0, -1); return; }   /* not a number: the old value comes back */
        if (isint) { int iv = (int)v; iv = iv < it->ilo ? it->ilo : iv > it->ihi ? it->ihi : iv; p->edited(k, 0, iv, -1); }
        else { v = v < it->lo ? it->lo : v > it->hi ? it->hi : v; p->edited(k, v, 0, -1); }   /* clamped as nk_property */
    });
    if (lk) {
        le->installEventFilter(new SoftLockFocus(le, p, k));
        it->lock = new QCheckBox; hl->addWidget(it->lock, 16);
        QCheckBox *cb = it->lock;
        QObject::connect(cb, &QCheckBox::toggled, cb, [p, k](bool on) { p->edited(k, 0, 0, on ? 1 : 0); });
    }
    it->w = le; cur(p)->addWidget(row); p->items_.append(it);
}
static void op_num(void *u, const char *label, const char *id, double *v, int *lk, int en, double lo, double hi) {
    GeoPanel *p = P(u); int k = p->k_++;
    if (p->mode_ == 0) { auto *it = new GeoPanel::Item; it->kind = IT_NUM; it->lo = lo; it->hi = hi; numRow(p, k, it, label, lk != nullptr, false); }
    else if (p->mode_ == 1 && p->pend_.k == k && en) { if (p->pend_.lock >= 0) lockWrite(lk, p->pend_.lock); else *v = p->pend_.d; }
    if (k < p->items_.size()) {
        GeoPanel::Item *it = p->items_[k]; auto *le = static_cast<QLineEdit *>(it->w);
        if (!le->hasFocus() || p->mode_ == 1) { QSignalBlocker b(le); le->setText(numTxt(*v, en != 0)); }
        le->setEnabled(en != 0);
        if (it->lock) { QSignalBlocker b(it->lock); it->lock->setChecked(lk && (*lk & GEO_LK_HARD)); it->lock->setEnabled(en != 0); }   /* IsLocked = IsHardLocked */
        it->soft = lk && (*lk & GEO_LK_SOFT);
    }
}
static void op_inum(void *u, const char *label, const char *id, int *v, int *lk, int en, int lo, int hi) {
    GeoPanel *p = P(u); int k = p->k_++;
    if (p->mode_ == 0) { auto *it = new GeoPanel::Item; it->kind = IT_INUM; it->ilo = lo; it->ihi = hi; numRow(p, k, it, label, lk != nullptr, true); }
    else if (p->mode_ == 1 && p->pend_.k == k && en) { if (p->pend_.lock >= 0) lockWrite(lk, p->pend_.lock); else *v = p->pend_.i; }
    if (k < p->items_.size()) {
        GeoPanel::Item *it = p->items_[k]; auto *le = static_cast<QLineEdit *>(it->w);
        if (!le->hasFocus() || p->mode_ == 1) { QSignalBlocker b(le); le->setText(QString::number(*v)); }
        le->setEnabled(en != 0);
        if (it->lock) { QSignalBlocker b(it->lock); it->lock->setChecked(lk && (*lk & GEO_LK_HARD)); it->lock->setEnabled(en != 0); }   /* IsLocked = IsHardLocked */
        it->soft = lk && (*lk & GEO_LK_SOFT);
    }
}
static void op_combo(void *u, const char **items, int n, int *ix, int greyed) {
    GeoPanel *p = P(u); int k = p->k_++;
    if (p->mode_ == 0) {
        auto *it = new GeoPanel::Item; it->kind = IT_COMBO;
        auto *cb = new QComboBox;
        for (int i = 0; i < n; i++) cb->addItem(fpTxt(items[i]));
        if (auto *m = qobject_cast<QStandardItemModel *>(cb->model())) if (greyed >= 0 && greyed < n) m->item(greyed)->setEnabled(false);   /* not ported */
        QObject::connect(cb, &QComboBox::currentIndexChanged, cb, [p, k](int i) { p->edited(k, 0, i, -1); });
        it->w = cb; cur(p)->addWidget(cb); p->items_.append(it);
    } else if (p->mode_ == 1 && p->pend_.k == k) *ix = p->pend_.i;
    if (k < p->items_.size()) { auto *cb = static_cast<QComboBox *>(p->items_[k]->w); QSignalBlocker b(cb); cb->setCurrentIndex(*ix); }
}
/* a tab's start (core_ui geo_panel_tabs order): every tab is walked, each into its own page under the body (shown for the
   tab bar's current tab); not an item (k_ unchanged) */
static int op_tab(void *u, int ix, const char *) {
    GeoPanel *p = P(u);
    if (p->mode_ == 0) {
        while (p->stack_.size() > 1) p->stack_.removeLast();
        auto *w = new QWidget; auto *l = new QVBoxLayout(w); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(2);
        p->stack_.first()->addWidget(w); p->tabw_.append(w); p->stack_.append(l);
        w->setVisible(ix == p->tabIndex());
    }
    return 1;
}
/* TextBox (the Bounding Box panel's tool plane Name): the text is written through the walk (mode 1) as the numbers */
static void op_text(void *u, const char *label, const char *, char *buf, int n, int en) {
    GeoPanel *p = P(u); int k = p->k_++;
    if (p->mode_ == 0) {
        if (label) { auto *l = new QLabel(fpTxt(label)); cur(p)->addWidget(l); }
        auto *it = new GeoPanel::Item; it->kind = IT_TEXT; auto *le = new QLineEdit;
        QObject::connect(le, &QLineEdit::editingFinished, le, [p, k, le] {
            if (!le->isModified()) return;
            le->setModified(false); p->pend_.s = le->text(); p->edited(k, 0, 0, -1);
        });
        it->w = le; cur(p)->addWidget(le); p->items_.append(it);
    } else if (p->mode_ == 1 && p->pend_.k == k && en) { QByteArray b = p->pend_.s.toUtf8(); snprintf(buf, (size_t)n, "%s", b.constData()); }
    if (k < p->items_.size()) {
        auto *le = static_cast<QLineEdit *>(p->items_[k]->w);
        if (!le->hasFocus() || p->mode_ == 1) { QSignalBlocker b(le); le->setText(QString::fromUtf8(buf)); }
        le->setEnabled(en != 0);
    }
}
static const CuiPanelOps QT_PANEL = { op_exp, op_end, op_label, op_radio, op_check, op_toggle, op_num, op_inum, op_combo, op_tab, op_text, op_tbtn };

GeoPanel::GeoPanel(QWidget *parent) : QWidget(parent) {
    auto *v = new QVBoxLayout(this); v->setContentsMargins(4, 4, 4, 4); v->setSpacing(4);
    /* header: the pane's caption is the dock's title (the command name - the FunctionPanel caption source is NOT
       DECODED; mcamui draws it as the first row), then the FunctionPanel buttons OK / OK-New / Cancel / Help
       (HasOKButton HasOKNewButton HasCancelButton HasHelpButton; texts as mcamui's, the WPF button images are not
       used), then the "Basic" tab */
    title_ = new QLabel(this); title_->hide();
    auto *br = new QHBoxLayout; br->setSpacing(4);
    const char *bt[3] = { "OK", "Apply", "Cancel" }; int code[3] = { 0, 1, 2 };
    for (int i = 0; i < 3; i++) { auto *b = new QPushButton(bt[i]); int c = code[i]; connect(b, &QPushButton::clicked, this, [this, c] { emit button(c); }); br->addWidget(b); btn_[i] = b; }
    auto *help = new QPushButton("Help"); help->setEnabled(false); br->addWidget(help);   /* HelpCommand: topic NOT DECODED */
    v->addLayout(br);
    tabs_ = new QTabBar; tabs_->addTab("Basic"); tabs_->setExpanding(false); v->addWidget(tabs_);   /* the panel's tabs (geo_panel_tabs) */
    connect(tabs_, &QTabBar::currentChanged, this, [this](int ix) { for (int i = 0; i < tabw_.size(); i++) tabw_[i]->setVisible(i == ix); });
    scroll_ = new QScrollArea; scroll_->setWidgetResizable(true); scroll_->setFrameShape(QFrame::NoFrame);
    v->addWidget(scroll_, 1);
}

int GeoPanel::tabIndex() const { return tabs_ ? tabs_->currentIndex() : 0; }
void GeoPanel::walk(int mode) {
    if (!geo_cmd || geo_cmd != cmd_) return;
    mode_ = mode; k_ = 0;
    geo_panel_body(&QT_PANEL, this);
    mode_ = 2;
}

void GeoPanel::rebuild() {
    cmd_ = geo_cmd;
    for (Item *it : items_) delete it;
    items_.clear();
    title_->setText(QString::fromUtf8(geo_name(cmd_)));
    tabw_.clear();
    {   QSignalBlocker b(tabs_); int nt = 1; const char *const *tn = cmd_ ? geo_panel_tabs(&nt) : nullptr;
        while (tabs_->count() > 0) tabs_->removeTab(0);
        for (int t = 0; t < nt; t++) tabs_->addTab(tn ? QString::fromUtf8(tn[t]) : QString("Basic"));
        tabs_->setCurrentIndex(0); }
    body_ = new QWidget; auto *bl = new QVBoxLayout(body_); bl->setContentsMargins(0, 0, 0, 0); bl->setSpacing(2);
    stack_.clear(); stack_.append(bl);
    if (cmd_) { geo_panel_sync(); walk(0); }
    bl->addStretch(1);
    retireWidget(scroll_->takeWidget());               /* the previous body (retire.h; setWidget would delete it now) */
    scroll_->setWidget(body_);
    stack_.clear();
    syncEnabled();
}

/* the chain commands: the panel takes no input while chain_it runs (EnableWindow(panel, 0)), Apply follows EnableApply
   (core_ui geo_panel_enabled / geo_panel_apply_enabled) */
void GeoPanel::syncEnabled() {
    bool en = !cmd_ || geo_panel_enabled();
    for (int i = 0; i < 3; i++) if (btn_[i]) btn_[i]->setEnabled(en && (i != 1 || !cmd_ || geo_panel_apply_enabled()));
    if (scroll_) scroll_->setEnabled(en);
}
void GeoPanel::refresh() { if (cmd_ && cmd_ == geo_cmd) walk(2); syncEnabled(); }

/* a widget changed (k = its item; -1 = only refresh): the walk writes the value through the body's pointer (its own
   post-processing runs: Diameter -> Radius, the trim-mode pick reset ...), then the model is synced, the changed keys
   stored and the live result rebuilt (mcamui geo_panel_content: sync + pk_note after the body, sync + live update
   on the next frame), and the widgets refreshed */
void GeoPanel::edited(int k, double d, int i, int lock) {
    if (!cmd_ || cmd_ != geo_cmd || mode_ != 2) return;
    if (k >= 0) {
        pend_.k = k; pend_.d = d; pend_.i = i; pend_.lock = lock;
        walk(1);
        pend_.k = -1;
        int c = cmd_;
        auto fn = [c] { if (geo_cmd == c) { geo_panel_sync(); pk_note(c); geo_live_update(); } };
        if (withGL) withGL(fn); else fn();
    }
    walk(2);
    emit changed();
}

/* ---- tests: the controls in walk order (index, kind, text, value, enabled), and a change made through the widget
   itself (its signal runs the same path as a user's edit) */
void GeoPanel::dump() const {
    static const char *kn[] = { "exp", "label", "radio", "check", "toggle", "num", "int", "combo", "text", "tbtn" };
    printf("    panel '%s' %d controls\n", geo_name(cmd_), (int)items_.size());
    for (int k = 0; k < items_.size(); k++) {
        const Item *it = items_[k]; QString t, v; bool en = true;
        if (it->kind == IT_EXP) { t = it->hdr->text(); v = it->box->isVisible() || !it->box->isHidden() ? "open" : "closed"; }
        else if (it->kind == IT_LABEL) t = static_cast<QLabel *>(it->w)->text();
        else if (it->kind == IT_TBTN) { for (auto *b : it->w->findChildren<QPushButton *>()) { t += (t.isEmpty() ? "" : " ") + b->text(); v += QString(v.isEmpty() ? "" : " ") + (b->isChecked() ? "on" : "off") + (b->isEnabled() ? "" : "(greyed)"); } }
        else if (it->kind == IT_RADIO || it->kind == IT_CHECK) { auto *b = static_cast<QAbstractButton *>(it->w); t = b->text(); v = b->isChecked() ? "on" : "off"; en = b->isEnabled(); }
        else if (it->kind == IT_TOGGLE) { auto *b = it->w->findChild<QPushButton *>(); t = it->val >= 0 && b ? b->text() : "(not ported)"; en = it->val >= 0 && b && b->isEnabled() && scroll_->isEnabled(); }
        else if (it->kind == IT_NUM || it->kind == IT_INUM) { auto *le = static_cast<QLineEdit *>(it->w); v = le->text(); en = le->isEnabled(); if (it->lock) v += it->lock->isChecked() ? " locked" : " unlocked"; if (it->soft) v += " soft"; }
        else if (it->kind == IT_COMBO) { auto *cb = static_cast<QComboBox *>(it->w); v = cb->currentText(); }
        else if (it->kind == IT_TEXT) { auto *le = static_cast<QLineEdit *>(it->w); v = le->text(); en = le->isEnabled(); }
        printf("      %2d %-6s %-28s %s%s\n", k, kn[it->kind], t.toUtf8().constData(), v.toUtf8().constData(), en ? "" : " (greyed)");
    }
}
bool GeoPanel::testFocus(int k) {                       /* the user clicks into box k and leaves it without typing (its soft lock) */
    if (k < 0 || k >= items_.size() || (items_[k]->kind != IT_NUM && items_[k]->kind != IT_INUM) || !items_[k]->w->isEnabled()) return false;
    QFocusEvent fe(QEvent::FocusIn, Qt::MouseFocusReason); QCoreApplication::sendEvent(items_[k]->w, &fe); return true;
}
bool GeoPanel::testSet(int k, const QString &v, bool lock) {
    if (k < 0 || k >= items_.size()) return false;
    Item *it = items_[k];
    if (lock) { if (!it->lock || !it->lock->isEnabled()) return false; it->lock->setChecked(v.toInt() != 0); return true; }
    switch (it->kind) {
    case IT_TBTN: { auto bl = it->w->findChildren<QPushButton *>(); int i = v.toInt() - 1;   /* the button (1-based) clicked */
        if (i < 0 || i >= bl.size() || !bl[i]->isEnabled()) return false; bl[i]->click(); return true; }
    case IT_RADIO: case IT_CHECK: { auto *b = static_cast<QAbstractButton *>(it->w); if (!b->isEnabled()) return false;
        if (it->kind == IT_RADIO) b->setChecked(true); else b->setChecked(v.toInt() != 0); return true; }
    case IT_TEXT: { auto *le = static_cast<QLineEdit *>(it->w); if (!le->isEnabled()) return false;
        le->setText(v); le->setModified(true); emit le->editingFinished(); return true; }
    case IT_NUM: case IT_INUM: { auto *le = static_cast<QLineEdit *>(it->w); if (!le->isEnabled()) return false;
        if (it->lock) { QFocusEvent fe(QEvent::FocusIn, Qt::MouseFocusReason); QCoreApplication::sendEvent(le, &fe); }   /* the user clicks into the box (its soft lock) */
        le->setText(v); le->setModified(true); emit le->editingFinished(); return true; }
    case IT_COMBO: static_cast<QComboBox *>(it->w)->setCurrentIndex(v.toInt()); return true;
    case IT_EXP: it->hdr->click(); return true;
    case IT_TOGGLE: { auto *b = it->w->findChild<QPushButton *>(); if (it->val < 0 || !b || !scroll_->isEnabled()) return false; b->click(); return true; }
    }
    return false;
}
