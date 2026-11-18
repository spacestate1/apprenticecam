#include "toolmgr.h"
#include "textfit.h"
#include "icons.h"
#include <QTableWidget>
#include <QListWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QGroupBox>
#include <QFrame>
#include <QMenu>
#include <QLineEdit>
#include <QScrollArea>
#include <QFileDialog>
#include <QPainter>
#include <QMouseEvent>
#include <QApplication>
#include <QFontMetrics>
#include <cmath>
#include <cstdio>
#include <cstring>

/* the templates' font (MS Shell Dlg 8 = 11 px at 96 dpi) and base units 6 x 13 px (chainDu, phase 4; mcamui BS_DX / BS_DY) */
static const float DX = 1.5f, DY = 1.625f;
static void dlgFont(QWidget *w) { w->setFont(TextFit::dialogFont(w->font())); }
static QRect du(int x, int y, int w, int h) { return chainDu(nullptr, x, y, w, h); }
static QRect px(const TmRect &r) { return QRect((int)lroundf(r.x), (int)lroundf(r.y), (int)lroundf(r.w), (int)lroundf(r.h)); }
template <class T> static T *mk(QWidget *parent) { T *w = new T(parent); dlgFont(w); w->show(); return w; }
template <class T> static T *mk(QWidget *parent, const QRect &r) { T *w = mk<T>(parent); w->setGeometry(r); return w; }
static QToolButton *bmpButton(QWidget *parent, const char *bmp, const char *tip) {   /* mcamui bp_btn: the bitmap, the tooltip */
    auto *b = mk<QToolButton>(parent); QString f = QString("#file:%1.png").arg(bmp);
    if (Icons::hasImage(f)) { QPixmap pm = Icons::image(f); b->setIcon(QIcon(pm)); b->setIconSize(pm.size()); } else b->setText(QString::fromUtf8(tip).left(1));
    b->setToolTip(QString::fromUtf8(tip)); return b;
}
static const QColor SEL_BG(51, 153, 255);               /* mcamui's selected row */

/* SS_PATHELLIPSIS as mcamui tm_path_ellipsis: the folder part shortened with "...", the file name kept */
static QString pathEllipsis(const QString &p, int maxw, const QFontMetrics &fm) {
    if (fm.horizontalAdvance(p) <= maxw) return p;
    int bs = qMax(p.lastIndexOf('/'), p.lastIndexOf('\\')); QString base = bs >= 0 ? p.mid(bs) : p;
    int keep = p.size() - base.size();
    while (keep > 3) { keep--; QString o = p.left(keep) + "..." + base; if (fm.horizontalAdvance(o) <= maxw) return o; }
    return "..." + base;
}
/* a list item's image: the CMillToolListCtrl strip cell (core_ui tm_item_strip / tm_item_cell); no pack: the letter tile */
static QIcon itemIcon(const Tool *t, int used, int large) {
    int sz = large ? 96 : 32; QString f = QString("#file:%1.png").arg(tm_item_strip(used, large));
    QPixmap strip = Icons::image(f);
    if (strip.isNull() || strip.width() < (tm_item_cell(t) + 1) * sz) return Icons::icon(f, sz, sz);
    return QIcon(strip.copy(tm_item_cell(t) * sz, 0, sz, sz));
}

/* ------------------------------------------------------------------------------------------------ ToolManagerDialog */
/* the resizable window's grip (WS_THICKFRAME; WM_GETMINMAXINFO: the template size is the minimum, FUN_1808790f0) */
class TmGrip : public QWidget {
public:
    TmGrip(ToolManagerDialog *d, QWidget *parent) : QWidget(parent), d_(d) { setCursor(Qt::SizeFDiagCursor); resize(14, 14); }
protected:
    void paintEvent(QPaintEvent *) override { QPainter p(this); p.setPen(QColor(150, 156, 165)); for (int k = 3; k < 14; k += 4) p.drawLine(width() - 1, height() - 1 - k, width() - 1 - k, height() - 1); }
    void mousePressEvent(QMouseEvent *e) override { start_ = e->globalPosition().toPoint(); size0_ = parentWidget()->size(); }
    void mouseMoveEvent(QMouseEvent *e) override { if (e->buttons() & Qt::LeftButton) { QPoint dd = e->globalPosition().toPoint() - start_; d_->resizeBody(size0_.width() + dd.x(), size0_.height() + dd.y()); } }
private:
    ToolManagerDialog *d_; QPoint start_; QSize size0_;
};

ToolManagerDialog::ToolManagerDialog(QWidget *parent) : FloatDlg("Tool Manager", parent) {
    setObjectName("toolManager");
    QWidget *b = body();
    setBodySize(QSize((int)lroundf(600 * DX), (int)lroundf(350 * DY)));   /* the template 600 x 350 du */
    group_ = mk<QComboBox>(b); group_->addItem("Machine Group-1");     /* 25276 machine group (one group in this program) */
    bmp_ = mk<QLabel>(b); bmp_->setPixmap(Icons::icon("#file:mccoreres_BMP_25046.png", 16, 16).pixmap(16, 16)); bmp_->setAlignment(Qt::AlignCenter);
    used_ = mk<QLabel>(b); used_->setText("= Tool used in an operation");   /* 25331 */
    plbl_ = mk<QLabel>(b); plbl_->setText("(Part)"); llbl_ = mk<QLabel>(b); llbl_->setText("(Library)");   /* 25457 / 25456 */
    view_ = mk<QComboBox>(b); view_->addItems({ "Both", "Part Tools", "Library Tools" });   /* 25297: 5414 / 5460 / 5464 */
    connect(view_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { act([i] { tm_view = i; }); });
    for (int L = 0; L < 2; L++) {
        fa_[L] = mk<QCheckBox>(b); fa_[L]->setText("Filter Active");   /* 25325 / 25263 */
        connect(fa_[L], &QCheckBox::toggled, this, [this, L](bool on) { if (!filling_) act([L, on] { tm_factive[L] = on; }); });
        auto *t = tab_[L] = mk<QTableWidget>(b);                       /* 25464 / 25463 CMillToolListCtrl (Details) */
        t->verticalHeader()->hide(); t->setSelectionMode(QAbstractItemView::NoSelection); t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setShowGrid(false); t->setWordWrap(false); t->setTextElideMode(Qt::ElideNone); t->setFocusPolicy(Qt::NoFocus);
        t->setStyleSheet(QString("QTableWidget { %1 }").arg(TextFit::dialogFontCss())); t->horizontalHeader()->setStyleSheet(QString("QHeaderView::section { %1 padding: 1px 3px; }").arg(TextFit::dialogFontCss()));
        t->horizontalHeader()->setFixedHeight(18); t->horizontalHeader()->setHighlightSections(false); t->horizontalHeader()->setSectionsClickable(true);
        t->horizontalHeader()->setMinimumSectionSize(4);
        connect(t, &QTableWidget::cellPressed, this, [this, L](int r, int) { if (QApplication::mouseButtons() & Qt::LeftButton) clickRow(L, r, QApplication::keyboardModifiers(), false); });
        connect(t, &QTableWidget::cellDoubleClicked, this, [this, L](int r, int) { if (r >= 0 && r < rows_[L].size()) { int i = rows_[L][r]; act([L, i] { tm_dblclick(L, i); }); } });
        t->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(t, &QTableWidget::customContextMenuRequested, this, [this, L, t](const QPoint &p) {
            int r = t->rowAt(p.y());
            if (r >= 0) clickRow(L, r, Qt::NoModifier, true); else { tm_focus = L; }
            popupMenu(L, t->viewport()->mapToGlobal(p));
        });
        connect(t->horizontalHeader(), &QHeaderView::sectionClicked, this, [this, L](int s) {   /* SortList: columns >= 2 */
            if (filling_ || s < 0 || s >= cols_[L].size() || cols_[L][s] < 2) return;
            int c = cols_[L][s]; act([L, c] { tm_sortlist(L, c); });
        });
        connect(t->horizontalHeader(), &QHeaderView::sectionResized, this, [this, L](int s, int, int nw) {   /* HDN_TRACK: the width persists */
            if (filling_ || s < 0 || s >= cols_[L].size() || cols_[L][s] < 1) return;
            TM_SET(L)->w[cols_[L][s]] = nw < 4 ? 4 : nw;
        });
        t->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(t->horizontalHeader(), &QHeaderView::customContextMenuRequested, this, [this, L, t](const QPoint &p) { columnMenu(L, t->horizontalHeader()->mapToGlobal(p)); });
        auto *ic = ico_[L] = mk<QListWidget>(b);                       /* LVS_ICON / LVS_SMALLICON (spacing NOT DECODED: Qt's) */
        ic->setSelectionMode(QAbstractItemView::NoSelection); ic->setFocusPolicy(Qt::NoFocus); ic->setMovement(QListView::Static); ic->setResizeMode(QListView::Adjust);
        ic->setStyleSheet(QString("QListWidget { %1 }").arg(TextFit::dialogFontCss()));
        connect(ic, &QListWidget::itemPressed, this, [this, L](QListWidgetItem *it) { if (QApplication::mouseButtons() & Qt::LeftButton) clickRow(L, it->data(Qt::UserRole).toInt(), QApplication::keyboardModifiers(), false); });
        connect(ic, &QListWidget::itemDoubleClicked, this, [this, L](QListWidgetItem *it) { int r = it->data(Qt::UserRole).toInt(); if (r >= 0 && r < rows_[L].size()) { int i = rows_[L][r]; act([L, i] { tm_dblclick(L, i); }); } });
        ic->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(ic, &QListWidget::customContextMenuRequested, this, [this, L, ic](const QPoint &p) {
            QListWidgetItem *it = ic->itemAt(p);
            if (it) clickRow(L, it->data(Qt::UserRole).toInt(), Qt::NoModifier, true); else tm_focus = L;
            popupMenu(L, ic->viewport()->mapToGlobal(p));
        });
        for (QWidget *vp : { t->viewport(), ic->viewport() }) vp->installEventFilter(this);
    }
    up_ = bmpButton(b, "mccoreres_BMP_25002", "Add a Library Tool to Part"); down_ = bmpButton(b, "mccoreres_BMP_25001", "Add a Part Tool to Library");   /* 25460 / 25461 */
    connect(up_, &QToolButton::clicked, this, [this] { act([] { tm_up(); }); });
    connect(down_, &QToolButton::clicked, this, [this] { act([] { tm_down(); }); });
    path_ = mk<QLabel>(b);                                              /* 10253: library path (SS_PATHELLIPSIS) */
    bnew_ = bmpButton(b, "mccoreres_BMP_25026", "New Library"); bbrw_ = bmpButton(b, "mccoreres_BMP_5543", "Browse Libraries"); bimp_ = bmpButton(b, "mccoreres_BMP_25010", "Import Tools");
    connect(bnew_, &QToolButton::clicked, this, [this] { fileAct(0); });
    connect(bbrw_, &QToolButton::clicked, this, [this] { fileAct(1); });
    connect(bimp_, &QToolButton::clicked, this, [this] { fileAct(2); });
    filt_ = mk<QPushButton>(b); filt_->setText("Filter...");            /* 25193 */
    connect(filt_, &QPushButton::clicked, this, [this] { act([] { tm_open_filter(); }); });
    grp_ = mk<QGroupBox>(b); grp_->setTitle("Display mode"); grp_->lower();   /* 10407 */
    static const char *dm[3] = { "Tools", "Assemblies", "Both" };      /* 10408-10410 */
    for (int k = 0; k < 3; k++) { rad_[k] = mk<QRadioButton>(b); rad_[k]->setText(dm[k]); rad_[k]->setAutoExclusive(false);
        connect(rad_[k], &QRadioButton::clicked, this, [this, k] { act([k] { tm_dispmode = k; }); }); }   /* SetListDisplayMode: each mode its own column set */
    ok_ = mk<QPushButton>(b); ok_->setText("OK"); help_ = mk<QPushButton>(b); help_->setText("Help"); help_->setEnabled(false);   /* Help: not ported */
    connect(ok_, &QPushButton::clicked, this, [this] { if (!tmj.active) act([] { tm_ok(); }); });
    connect(this, &FloatDlg::closeClicked, this, [this] { act([] { tm_cancel(); }); });   /* the close box = IDCANCEL */
    /* the prompts inside the dialog (mcamui popups): 3840 «Deleting Tools», 5258 «Similar Tool Warning», 3842 «Save Library?» */
    prompt_ = mk<QFrame>(b); prompt_->setFrameShape(QFrame::Box); prompt_->setAutoFillBackground(true); prompt_->hide();
    promptText_ = mk<QLabel>(prompt_); promptText_->setWordWrap(true); promptText_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    promptDont_ = mk<QCheckBox>(prompt_); promptDont_->setText("Don't show this again");   /* checkbox text: NOT DECODED (mcamui's) */
    promptYes_ = mk<QPushButton>(prompt_); promptYes_->setText("Yes"); promptNo_ = mk<QPushButton>(prompt_); promptNo_->setText("No");
    connect(promptDont_, &QCheckBox::toggled, this, [this](bool on) { if (!filling_ && tmq.kind && tmq.dont_box) tmq.dont_box = on ? 2 : 1; });
    connect(promptYes_, &QPushButton::clicked, this, [this] { act([] { if (tm_confirm) tm_confirm_answer(1); else if (tmq.kind) tm_q_answer(1); }); });
    connect(promptNo_, &QPushButton::clicked, this, [this] { act([] { if (tm_confirm) tm_confirm_answer(0); else if (tmq.kind) tm_q_answer(0); }); });
    grip_ = new TmGrip(this, b); grip_->show();
    relayout();
}
void ToolManagerDialog::resizeBody(int w, int h) {
    int mw = (int)lroundf(600 * DX), mh = (int)lroundf(350 * DY);
    setBodySize(QSize(qMax(w, mw), qMax(h, mh)));
    relayout();
}
/* a model action, then the widgets (queued: the list or button that sent the signal may be rebuilt) */
void ToolManagerDialog::act(const std::function<void()> &fn) {
    if (filling_) return;
    fn();
    QMetaObject::invokeMethod(this, [this] { refresh(true); emit changed(); }, Qt::QueuedConnection);
}
void ToolManagerDialog::relayout() {
    QWidget *b = body(); TmLayout Lo; tm_layout((float)b->width(), (float)b->height(), DX, DY, &Lo);   /* OnSize FUN_18087a9c0 */
    int sp, sl, sud, sdm; tm_shown(&sp, &sl, &sud, &sdm);                 /* FUN_18087a050 */
    group_->setGeometry(px(Lo.combo).adjusted(0, -2, 0, 4)); group_->setVisible(sp);
    bmp_->setGeometry(px(Lo.bmp)); bmp_->setVisible(sp);
    used_->setGeometry(px(Lo.used_lbl).adjusted(0, -2, 60, 2)); used_->setVisible(sp);
    plbl_->setGeometry(px(Lo.plbl).adjusted(0, -2, 10, 2)); plbl_->setVisible(sp);
    for (int L = 0; L < 2; L++) {
        bool on = L == 0 ? sp : sl; QRect r = px(L == 0 ? Lo.plist : Lo.llist);
        tab_[L]->setGeometry(r); ico_[L]->setGeometry(r);
        bool det = tm_listview[L] == 1;
        tab_[L]->setVisible(on && det); ico_[L]->setVisible(on && !det);
        fa_[L]->setGeometry(px(L == 0 ? Lo.pfa : Lo.lfa).adjusted(0, -2, 20, 4)); fa_[L]->setVisible(on);
    }
    view_->setGeometry(px(Lo.view).adjusted(0, -2, 0, 4));
    up_->setGeometry(px(Lo.up)); down_->setGeometry(px(Lo.down)); up_->setVisible(sud); down_->setVisible(sud);
    path_->setGeometry(px(Lo.path)); bnew_->setGeometry(px(Lo.bnew)); bbrw_->setGeometry(px(Lo.bbrw)); bimp_->setGeometry(px(Lo.bimp));
    for (QWidget *w : { (QWidget *)path_, (QWidget *)bnew_, (QWidget *)bbrw_, (QWidget *)bimp_, (QWidget *)llbl_ }) w->setVisible(sl);
    llbl_->setGeometry(px(Lo.llbl).adjusted(0, -2, 10, 2));
    filt_->setGeometry(px(Lo.filt));
    grp_->setGeometry(px(Lo.grp)); grp_->setVisible(sdm);
    for (int k = 0; k < 3; k++) { rad_[k]->setGeometry(px(Lo.rad[k]).adjusted(0, -2, 0, 4)); rad_[k]->setVisible(sdm); }
    ok_->setGeometry(px(Lo.ok)); help_->setGeometry(px(Lo.help));
    grip_->move(b->width() - grip_->width(), b->height() - grip_->height()); grip_->raise();
}
/* what the dialog shows: a refresh when it changed (hover redraws would otherwise rebuild the lists) */
static QByteArray tmSig() {
    QByteArray s; s.append((const char *)tm_sel, sizeof tm_sel); s.append((const char *)tm_cs, sizeof tm_cs);
    int v[18] = { tm_view, tm_dispmode, tm_factive[0], tm_factive[1], tm_listview[0], tm_listview[1], F.ntools, tlib_ok ? tlib.n : -1, tmq.kind, tmq.dont_box,
                  tm_confirm, tmj.active, tm_nclip, ncamops, F.nops, tm_focus, show_tm, tmf_open };
    s.append((const char *)v, sizeof v); s.append((const char *)&tmf, sizeof tmf);
    if (tlib_ok) s.append(tlib.path);
    for (int i = 0; i < F.ntools; i++) { const Tool *t = &F.tools[i]; s.append((const char *)&t->num, sizeof t->num); s.append((const char *)&t->dia, sizeof t->dia); s.append(t->name); }
    for (int i = 0; tlib_ok && i < tlib.n; i++) { const Tool *t = &tlib.t[i]; s.append((const char *)&t->num, sizeof t->num); s.append((const char *)&t->dia, sizeof t->dia); s.append(t->name); }
    for (int k = 0; k < ncamops; k++) s.append((const char *)&camops[k]->p.tool_num, sizeof(int));
    return s;
}
void ToolManagerDialog::fillList(int L) {
    int idx[4096], n = tm_rows(L, idx);
    rows_[L].clear(); for (int k = 0; k < n; k++) rows_[L] << idx[k];
    if (tm_listview[L] == 1) {                         /* Details: FillList 0x180506ca0 (core_ui tm_cell) */
        TmColSet *s = TM_SET(L); QTableWidget *t = tab_[L];
        cols_[L].clear(); for (int k = 0; k < TM_COLS; k++) { int c = s->ord[k]; if (c >= 1 && s->vis[c]) cols_[L] << c; }
        if (cols_[L].isEmpty()) cols_[L] << 1;
        t->clear(); t->setColumnCount(cols_[L].size()); t->setRowCount(n);
        QStringList hd; for (int c : cols_[L]) hd << QString::fromUtf8(tm_head[c]);
        t->setHorizontalHeaderLabels(hd);
        for (int k = 0; k < cols_[L].size(); k++) { int c = cols_[L][k]; t->setColumnWidth(k, c == 1 && !(s->vis[1] && s->w[1] > 4) ? 25 : (s->w[c] > 4 ? s->w[c] : 4)); }
        int sk = cols_[L].indexOf(s->sortcol);
        t->horizontalHeader()->setSortIndicatorShown(sk >= 0);
        if (sk >= 0) t->horizontalHeader()->setSortIndicator(sk, s->sortasc ? Qt::AscendingOrder : Qt::DescendingOrder);
        for (int r = 0; r < n; r++) {
            const Tool *tl = tm_tool(L, idx[r]); bool sel = idx[r] < 4096 && tm_sel[L][idx[r]];
            for (int k = 0; k < cols_[L].size(); k++) {
                int c = cols_[L][k]; auto *it = new QTableWidgetItem;
                if (c == 1) { int used = L == 0 && tm_used(tl);   /* image: 25046 used / 25045 unused (the part list only) */
                    it->setIcon(Icons::icon(used ? "#file:mccoreres_BMP_25046.png" : "#file:mccoreres_BMP_25045.png", 16, 16)); }
                else { char sx[160]; tm_cell(tl, c, sx, sizeof sx); it->setText(QString::fromUtf8(sx)); }
                if (sel) { it->setBackground(SEL_BG); it->setForeground(Qt::white); }
                t->setItem(r, k, it);
            }
            t->setRowHeight(r, 18);
        }
        TextFit::fitColumns(t);                       /* ours, user decision UI-2: columns fit their header / cells (filling_: not persisted) */
    } else {                                           /* Large Icons (0, 96 px) / Small Icons (3, 32 px) */
        QListWidget *ic = ico_[L]; int large = tm_listview[L] == 0;
        ic->clear();
        if (large) { ic->setViewMode(QListView::IconMode); ic->setIconSize(QSize(96, 96)); ic->setFlow(QListView::LeftToRight); ic->setWrapping(true); }
        else { ic->setViewMode(QListView::ListMode); ic->setIconSize(QSize(32, 32)); ic->setFlow(QListView::LeftToRight); ic->setWrapping(true); }
        for (int r = 0; r < n; r++) {
            const Tool *tl = tm_tool(L, idx[r]); char s[160]; tm_item_text(tl, s, sizeof s);   /* "%3ld - %s, %s" */
            auto *it = new QListWidgetItem(itemIcon(tl, L == 0 && tm_used(tl), large), QString::fromUtf8(s));
            it->setData(Qt::UserRole, r);
            if (idx[r] < 4096 && tm_sel[L][idx[r]]) { it->setBackground(SEL_BG); it->setForeground(Qt::white); }
            ic->addItem(it);
        }
    }
}
void ToolManagerDialog::refresh(bool force) {
    if (!show_tm) return;
    QByteArray s = tmSig();
    if (!force && s == sig_) return;
    sig_ = s; filling_ = true;
    relayout();
    view_->setCurrentIndex(tm_view);
    for (int L = 0; L < 2; L++) { fa_[L]->setChecked(tm_factive[L] != 0); fillList(L); }
    for (int k = 0; k < 3; k++) rad_[k]->setChecked(tm_dispmode == k);
    up_->setEnabled(tm_up_ok()); down_->setEnabled(tm_down_ok());
    QString lp = tlib_ok ? QString::fromUtf8(tlib.path) : QString();
    path_->setText(pathEllipsis(lp, path_->width(), path_->fontMetrics())); path_->setToolTip(lp);
    /* the prompt (mcamui: nuklear popups 420 x 130 / 520 x 150 or 120 at y 150, centred) */
    bool conf = tm_confirm != 0, q = tmq.kind != 0;
    if (conf || q) {
        QWidget *b = body(); int w = conf ? 420 : 520, h = conf ? 130 : tmq.dont_box ? 150 : 120;
        prompt_->setGeometry(b->width() / 2 - w / 2, 150, w, h);
        QString t = conf ? QString(tm_confirm == 1 ? "1 or more tools are used by operations\n" : "") + "Confirm deletion of tool(s)?"   /* 5585 / 3841 */
                         : QString::fromUtf8(tmq.title) + "\n\n" + QString::fromUtf8(tmq.text);
        promptText_->setGeometry(10, 8, w - 20, h - 50 - (q && tmq.dont_box ? 20 : 0)); promptText_->setText(t);
        promptDont_->setVisible(q && tmq.dont_box); promptDont_->setGeometry(10, h - 58, w - 20, 18); promptDont_->setChecked(q && tmq.dont_box == 2);
        promptYes_->setGeometry(10, h - 32, 75, 24); promptNo_->setGeometry(90, h - 32, 75, 24);
        prompt_->show(); prompt_->raise();
    } else prompt_->hide();
    filling_ = false;
}
/* a row click (core_ui tm_click: NM_SETFOCUS deselects the other list, Ctrl toggles, Shift a range from the anchor) */
void ToolManagerDialog::clickRow(int L, int row, Qt::KeyboardModifiers km, bool right) {
    if (filling_ || tmq.kind || tm_confirm || row < 0 || row >= rows_[L].size()) return;
    int idx[4096], n = rows_[L].size(); for (int k = 0; k < n; k++) idx[k] = rows_[L][k];
    tm_click(L, rows_[L][row], row, idx, n, (km & Qt::ControlModifier) != 0, (km & Qt::ShiftModifier) != 0, right);
    QMetaObject::invokeMethod(this, [this] { refresh(true); emit changed(); }, Qt::QueuedConnection);   /* the menu's items read the model */
}
bool ToolManagerDialog::eventFilter(QObject *o, QEvent *e) {
    if (e->type() == QEvent::MouseButtonPress) {      /* a click on the empty part of a list: that list takes the focus */
        auto *me = static_cast<QMouseEvent *>(e);
        for (int L = 0; L < 2; L++) {
            if (o == tab_[L]->viewport() && tab_[L]->rowAt(me->position().toPoint().y()) < 0 && me->button() == Qt::LeftButton) tm_focus = L;
            if (o == ico_[L]->viewport() && !ico_[L]->itemAt(me->position().toPoint()) && me->button() == Qt::LeftButton) tm_focus = L;
        }
    }
    return FloatDlg::eventFilter(o, e);
}
/* the modal key path (mcamui tm_keydown; LVN_KEYDOWN FUN_180879d10 / FUN_180879c10 = core_ui tm_key) */
bool ToolManagerDialog::keyDown(int k, bool ctrl) {
    if (k == Qt::Key_Escape) act([] { tm_escape(); });
    else if (!tmf_open) {
        int tk = k == Qt::Key_Insert ? TMK_INSERT : k == Qt::Key_Delete ? TMK_DELETE : k == Qt::Key_C ? TMK_C : k == Qt::Key_V ? TMK_V : TMK_NONE;
        if (tk) act([tk, ctrl] { tm_key(tm_focus, tk, ctrl); });
    }
    return true;
}
/* menu 1225 (core_ui tm_menu_items; cascades View / Arrange tools / Import/export tools) */
QMenu *ToolManagerDialog::popupMenu(int L, const QPoint &global) {
    auto *m = new QMenu(this); m->setAttribute(Qt::WA_DeleteOnClose);
    TmMItem it[64]; int n = tm_menu_items(L, it, 64); QMenu *sub = nullptr;
    for (int k = 0; k < n; k++) {
        if (it[k].depth == 0) sub = nullptr;
        if (it[k].act == TMA_SEP) { m->addSeparator(); continue; }
        if (it[k].sub) { sub = m->addMenu(QString::fromUtf8(it[k].text)); continue; }
        QMenu *into = it[k].depth && sub ? sub : m;
        QAction *a = into->addAction(QString::fromUtf8(it[k].text));
        if (it[k].checked) { a->setCheckable(true); a->setChecked(true); }
        a->setEnabled(it[k].en && it[k].act != TMA_NONE);
        int ac = it[k].act;
        connect(a, &QAction::triggered, this, [this, L, ac] {
            act([L, ac] { tm_menu_act(L, ac); });
            if (ac == TMA_SAVETO) fileAct(3); else if (ac == TMA_COMPRESS) fileAct(4);   /* 32946 / 32951: the file dialogs */
        });
    }
    m->popup(global);
    return m;
}
QMenu *ToolManagerDialog::columnMenu(int L, const QPoint &global) {   /* VisibleColumnPopupMenu: columns 2..12, ticked when visible */
    auto *m = new QMenu(this); m->setAttribute(Qt::WA_DeleteOnClose);
    for (int c = 2; c < TM_COLS; c++) {
        QAction *a = m->addAction(QString::fromUtf8(tm_head[c])); a->setCheckable(true); a->setChecked(TM_SET(L)->vis[c] != 0); a->setEnabled(tm_colmenu_ok(L, c));
        connect(a, &QAction::triggered, this, [this, L, c] { act([L, c] { tm_col_toggle(L, c); }); });
    }
    m->popup(global);
    return m;
}
/* the file dialogs (mcamui fd_show purposes FD_TM_NEW / FD_OPEN_LIB / FD_TM_IMPORT (+ FD_TM_IMPORT_LIB) / FD_TM_SAVETO /
   FD_TM_COMPRESS and their filters FDT_TOOLS / FDT_TOOLDB / FDT_PARTS 25537), then the core_ui actions */
void ToolManagerDialog::fileAct(int what) {
    char dir[1100]; tm_lib_dir(dir, sizeof dir); QString d = QString::fromUtf8(dir);
    const QString tooldb = "TOOLDB Files (*.tooldb);;All Files (*)", tools = "Tool library (*.tooldb);;All Files (*)", parts = "Mastercam Files (*.mcam *.mcx*);;All Files (*)";
    auto withExt = [](QString p) { QByteArray b = p.toUtf8(); char s[1400]; snprintf(s, sizeof s, "%s", b.constData()); tm_def_ext(s, sizeof s); return QString::fromUtf8(s); };
    QString p;
    if (what == 0) { p = QFileDialog::getSaveFileName(this, QString(), d + "/Untitled.tooldb", tools); if (!p.isEmpty()) { QString q = withExt(p); act([q] { tm_new(q.toUtf8().constData()); }); } }   /* New Library FUN_1808789a0 */
    else if (what == 1) { p = QFileDialog::getOpenFileName(this, QString(), tlib_ok ? QString::fromUtf8(tlib.path) : d, tooldb); if (!p.isEmpty()) act([p] { tm_browse(p.toUtf8().constData()); }); }   /* FUN_180876f70 */
    else if (what == 2) {                              /* Import Tools FUN_1808791c0: 25550 «Select file for tool import:», then the library to write */
        char id[1100], nm[400]; tm_import_start(id, sizeof id, nm, sizeof nm);
        p = QFileDialog::getOpenFileName(this, "Select file for tool import:", QString::fromUtf8(id) + "/" + QString::fromUtf8(nm), parts);
        if (p.isEmpty()) return;
        char td[1100], tn[400]; tm_import_target(p.toUtf8().constData(), td, sizeof td, tn, sizeof tn);
        QString l = QFileDialog::getSaveFileName(this, QString(), QString::fromUtf8(td) + "/" + QString::fromUtf8(tn), tools);
        if (!l.isEmpty()) { QString q = withExt(l); act([q] { tm_import_to(q.toUtf8().constData()); }); }
    }
    else if (what == 3) { p = QFileDialog::getSaveFileName(this, QString(), d + "/Untitled.tooldb", tools, nullptr, QFileDialog::DontConfirmOverwrite); if (!p.isEmpty()) { QString q = withExt(p); act([q] { tm_save_to(q.toUtf8().constData()); }); } }
    else if (what == 4) { p = QFileDialog::getOpenFileName(this, QString(), tlib_ok ? QString::fromUtf8(tlib.path) : d, tools); if (!p.isEmpty()) act([p] { tm_compress(p.toUtf8().constData()); }); }
}
void ToolManagerDialog::dump() const {
    printf("    tmdlg %dx%d view \"%s\" lib \"%s\" up %d down %d filter-active %d %d disp %d prompt %s\n", width(), height(), view_->currentText().toUtf8().constData(),
           path_->text().toUtf8().constData(), up_->isEnabled(), down_->isEnabled(), fa_[0]->isChecked(), fa_[1]->isChecked(),
           rad_[0]->isChecked() ? 0 : rad_[1]->isChecked() ? 1 : 2, prompt_->isVisible() ? promptText_->text().replace('\n', " / ").toUtf8().constData() : "-");
    for (int L = 0; L < 2; L++) {
        bool vis = tab_[L]->isVisible() || ico_[L]->isVisible();
        if (!vis) { printf("      list %d hidden\n", L); continue; }
        if (tab_[L]->isVisible()) {
            QTableWidget *t = tab_[L]; QStringList hd; for (int k = 0; k < t->columnCount(); k++) hd << QString("%1:%2").arg(t->horizontalHeaderItem(k) ? t->horizontalHeaderItem(k)->text() : "").arg(t->columnWidth(k));
            printf("      list %d details [%s]\n", L, hd.join(' ').toUtf8().constData());
            for (int r = 0; r < t->rowCount(); r++) { QStringList c; for (int k = 0; k < t->columnCount(); k++) c << (t->item(r, k) ? t->item(r, k)->text() : "");
                printf("        %s%s\n", t->item(r, 0) && t->item(r, 0)->background() == QBrush(SEL_BG) ? "*" : " ", c.join(" | ").toUtf8().constData()); }
        } else {
            QListWidget *ic = ico_[L]; printf("      list %d icons %d\n", L, ic->iconSize().width());
            for (int r = 0; r < ic->count(); r++) printf("        %s%s\n", ic->item(r)->background() == QBrush(SEL_BG) ? "*" : " ", ic->item(r)->text().toUtf8().constData());
        }
    }
}
bool ToolManagerDialog::testSet(const QString &key, const QString &v) {
    int L = v.section(',', 0, 0).toInt(), r = v.section(',', 1, 1).toInt(); QString m = v.section(',', 2, 2);
    if (key == "row" || key == "dbl") {                /* row=L,R[,c|s|r] / dbl=L,R: through the list widget's signals */
        if (L < 0 || L > 1 || r < 0 || r >= rows_[L].size()) return false;
        if (key == "dbl") { clickRow(L, r, Qt::NoModifier, false); if (tab_[L]->isVisible()) emit tab_[L]->cellDoubleClicked(r, 0); else emit ico_[L]->itemDoubleClicked(ico_[L]->item(r)); return true; }
        if (m == "r") { clickRow(L, r, Qt::NoModifier, true); return true; }
        clickRow(L, r, m == "c" ? Qt::ControlModifier : m == "s" ? Qt::ShiftModifier : Qt::NoModifier, false); return true;
    }
    if (key == "up") { if (!up_->isEnabled()) return false; up_->click(); return true; }
    if (key == "down") { if (!down_->isEnabled()) return false; down_->click(); return true; }
    if (key == "ok") { ok_->click(); return true; }
    if (key == "close") { emit closeClicked(); return true; }
    if (key == "filter") { filt_->click(); return true; }
    if (key == "view") { view_->setCurrentIndex(v.toInt()); emit view_->activated(v.toInt()); return true; }
    if (key == "fa") { if (L < 0 || L > 1 || !fa_[L]->isVisible()) return false; fa_[L]->setChecked(r != 0); return true; }
    if (key == "disp") { int k = v.toInt(); if (k < 0 || k > 2 || !rad_[k]->isVisible()) return false; rad_[k]->click(); return true; }
    if (key == "sort") {                               /* sort=L,C: a click on column C's header */
        int s = cols_[L].indexOf(r); if (s < 0 || !tab_[L]->isVisible()) return false; emit tab_[L]->horizontalHeader()->sectionClicked(s); return true; }
    if (key == "width") {                              /* width=L,C,W: a header edge drag */
        int s = cols_[L].indexOf(r); if (s < 0 || !tab_[L]->isVisible()) return false; tab_[L]->horizontalHeader()->resizeSection(s, m.toInt()); return true; }
    if (key == "col" || key == "menu") {               /* col=L,C (the column chooser) / menu=L,TEXT (menu 1225): an action of the menu */
        QMenu *mn = key == "col" ? columnMenu(L, mapToGlobal(QPoint(100, 100))) : popupMenu(L, mapToGlobal(QPoint(100, 100)));
        QString want = key == "col" ? QString::fromUtf8(tm_head[r < TM_COLS && r >= 0 ? r : 0]) : v.section(',', 1);
        std::function<QAction *(QMenu *)> find = [&](QMenu *q) -> QAction * {
            for (QAction *a : q->actions()) { if (a->menu()) { if (QAction *x = find(a->menu())) return x; } else if (a->text() == want) return a; } return nullptr; };
        QAction *a = find(mn); bool ok = a && a->isEnabled();
        if (ok) { int ac = 0; TmMItem it[64]; int n = tm_menu_items(L, it, 64); for (int k = 0; k < n; k++) if (it[k].text && want == it[k].text) ac = it[k].act;
                  if (key == "menu" && (ac == TMA_SAVETO || ac == TMA_COMPRESS)) ok = false; else a->trigger(); }   /* the file dialogs are not scripted */
        mn->close();
        return ok;
    }
    if (key == "ans") {                                /* ans=Y|N|dY|dN: the prompt's buttons (d = tick "Don't show this again" first) */
        if (!prompt_->isVisible()) return false;
        if (v.startsWith('d') && promptDont_->isVisible()) promptDont_->setChecked(true);
        (v.endsWith('Y') ? promptYes_ : promptNo_)->click(); return true;
    }
    if (key == "key") {                                /* key=Insert|Delete|Ctrl+C|Ctrl+V|Esc */
        bool ctrl = v.startsWith("Ctrl+"); QString k = ctrl ? v.mid(5) : v;
        int qk = k == "Insert" ? Qt::Key_Insert : k == "Delete" ? Qt::Key_Delete : k == "C" ? Qt::Key_C : k == "V" ? Qt::Key_V : k == "Esc" || k == "Escape" ? Qt::Key_Escape : 0;
        if (!qk) return false; keyDown(qk, ctrl); return true;
    }
    return false;
}

/* ------------------------------------------------------------------------------------------------ ToolFilterDialog */
/* mccoreres 25519 (473 x 218 du) at its template positions (formats/toolmanager_ui.md 1.4), the model tmf_edit (core_ui
   1.5.6): the owner-drawn type pictures are NOT DECODED (the caption on a toggle button, the type as tooltip), 1860 "Copy job
   setup matl" and Help greyed */
ToolFilterDialog::ToolFilterDialog(QWidget *parent) : FloatDlg("Tool List Filter", parent) {
    setClosable(false); setObjectName("toolFilter");
    setBodySize(du(0, 0, 473, 218).size());
    QWidget *b = body();
    auto group = [b](int x, int y, int w, int h, const char *t) { auto *g = mk<QGroupBox>(b, du(x, y, w, h)); g->setTitle(t); g->lower(); };
    group(8, 8, 234, 185, "Tool Types"); group(250, 8, 214, 38, "Tool Diameter"); group(250, 54, 214, 34, "Radius Type"); group(250, 96, 214, 84, "Tool Material");
    for (int k = 0; k < TMF_NTYPES; k++) {
        const TmfType &tb = TMF_TYPES[k]; auto *t = mk<QToolButton>(b, du(tb.x, tb.y, 30, 28));
        t->setCheckable(true); t->setText(QString::fromUtf8(tb.t)); t->setToolTip(QString::fromUtf8(tb.t)); t->setToolButtonStyle(Qt::ToolButtonTextOnly);
        QFont f = t->font(); f.setPixelSize(8); t->setFont(f);
        /* the type's bitmap (UI-5, the Mastercam-default rule): CToolFilt::InitBitmaps (decomp/mccore_toolfilt.c l.936-1145), mccoreres
           5038 + type for types 0-22, 10309 / 10473 / 10472 for 23 / 24 / 25, colour 0xFF00FF clear (type 0: 5058 with ProdEnableBits
           0x40, NOT DECODED here: 5038) */
        { int ty = tb.type, bmp = ty <= 22 ? 5038 + ty : ty == 23 ? 10309 : ty == 24 ? 10473 : 10472;
          QPixmap pm = TextFit::keyedBitmap(QString("mccoreres_BMP_%1.png").arg(bmp), 0xFF00FF);
          if (!pm.isNull()) { t->setIcon(QIcon(pm)); t->setIconSize(pm.size()); t->setToolButtonStyle(Qt::ToolButtonIconOnly); } }
        int ty = tb.type; connect(t, &QToolButton::toggled, this, [this, ty](bool on) { if (!filling_) tmf_edit.type[ty] = on; });
        types_ << t;
    }
    auto button = [this, b](int x, int y, int w, int h, const char *t, const std::function<void()> &fn) {
        auto *p = mk<QPushButton>(b, du(x, y, w, h)); p->setText(t); connect(p, &QPushButton::clicked, this, [this, fn] { fn(); refresh(); }); return p; };
    button(71, 141, 50, 14, "All", [] { tmf_types_all(1); }); button(127, 141, 50, 14, "None", [] { tmf_types_all(0); });   /* 1815 / 1817 */
    mk<QLabel>(b, du(34, 161, 63, 9))->setText("Operation masking"); mk<QLabel>(b, du(131, 161, 45, 9))->setText("Unit masking");
    op_ = mk<QComboBox>(b, du(34, 173, 87, 13).adjusted(0, -2, 0, 4)); for (int k = 0; k < 3; k++) op_->addItem(TMF_OPM[k]);   /* 2497 */
    unit_ = mk<QComboBox>(b, du(130, 173, 86, 13).adjusted(0, -2, 0, 4)); for (int k = 0; k < 3; k++) unit_->addItem(TMF_UM[k]);   /* 2502 */
    connect(op_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { tmf_edit.op = i; });
    connect(unit_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { tmf_edit.unit = i; });
    button(7, 198, 50, 14, "Reset all", [] { tmf_defaults(&tmf_edit); });   /* 1358 OnResetPb */
    dm_ = mk<QComboBox>(b, du(258, 24, 80, 13).adjusted(0, -2, 0, 4)); for (int k = 0; k < 5; k++) dm_->addItem(TMF_DM[k]);   /* 1772 */
    connect(dm_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { tmf_edit.dsel = i; refresh(); });   /* OnSelchangeDiaCmb */
    d1_ = mk<QLineEdit>(b, du(344, 24, 52, 14)); d2_ = mk<QLineEdit>(b, du(403, 24, 52, 14));   /* 1776 / 1779 */
    for (int k = 0; k < 2; k++) { QLineEdit *e = k ? d2_ : d1_;
        connect(e, &QLineEdit::editingFinished, this, [this, e, k] { double d; if (tw_parse_num(e->text().toUtf8().constData(), &d)) (k ? tmf_edit.d2 : tmf_edit.d1) = d; refresh(); }); }
    static const int rb[3][2] = { {258,70}, {321,70}, {382,70} };
    for (int k = 0; k < 3; k++) { rad_[k] = mk<QCheckBox>(b, du(rb[k][0], rb[k][1], 55, 10).adjusted(0, -2, 0, 4)); rad_[k]->setText(TMF_RAD[k]);
        connect(rad_[k], &QCheckBox::toggled, this, [this, k](bool on) { if (!filling_) tmf_edit.rad[k] = on; }); }
    static const int mb[6][2] = { {272,111}, {272,126}, {272,140}, {357,111}, {357,126}, {357,140} };
    for (int k = 0; k < 6; k++) { mat_[k] = mk<QCheckBox>(b, du(mb[k][0], mb[k][1], 75, 10).adjusted(0, -2, 0, 4)); mat_[k]->setText(TMF_MAT[k]);   /* relabelled 3711-3716 */
        connect(mat_[k], &QCheckBox::toggled, this, [this, k](bool on) { if (!filling_) tmf_edit.mat[k] = on; }); }
    button(260, 158, 50, 14, "All", [] { memset(tmf_edit.mat, 1, 6); }); button(316, 158, 50, 14, "None", [] { memset(tmf_edit.mat, 0, 6); });   /* 1856 / 1858 */
    mk<QPushButton>(b, du(372, 158, 82, 14))->setText("Copy job setup matl"); b->findChildren<QPushButton *>().last()->setEnabled(false);   /* 1860 NOT DECODED */
    ok_ = mk<QPushButton>(b, du(348, 196, 36, 18)); ok_->setText("OK"); cancel_ = mk<QPushButton>(b, du(388, 196, 36, 18)); cancel_->setText("Cancel");
    auto *help = mk<QPushButton>(b, du(428, 196, 36, 18)); help->setText("Help"); help->setEnabled(false);
    connect(ok_, &QPushButton::clicked, this, [this] { tmf_ok(); emit closed(); });       /* OnOK -> AllFalseToTrue (core_ui) */
    connect(cancel_, &QPushButton::clicked, this, [this] { tmf_open = 0; emit closed(); });
}
void ToolFilterDialog::refresh() {
    filling_ = true; const TmFilt *f = &tmf_edit;
    for (int k = 0; k < TMF_NTYPES; k++) types_[k]->setChecked(f->type[TMF_TYPES[k].type] != 0);
    op_->setCurrentIndex(f->op); unit_->setCurrentIndex(f->unit); dm_->setCurrentIndex(f->dsel);
    d1_->setVisible(f->dsel >= 1); d2_->setVisible(f->dsel == 4);   /* OnSelchangeDiaCmb */
    d1_->setText(QString::number(f->d1, 'g', 15)); d2_->setText(QString::number(f->d2, 'g', 15));
    for (int k = 0; k < 3; k++) rad_[k]->setChecked(f->rad[k] != 0);
    for (int k = 0; k < 6; k++) mat_[k]->setChecked(f->mat[k] != 0);
    filling_ = false;
}
void ToolFilterDialog::dump() const {
    QString t; for (auto *b : types_) t += b->isChecked() ? '1' : '0';
    QString r, m; for (auto *c : rad_) r += c->isChecked() ? '1' : '0'; for (auto *c : mat_) m += c->isChecked() ? '1' : '0';
    printf("    tfdlg types %s op \"%s\" unit \"%s\" dia \"%s\" %s%s rad %s mat %s\n", t.toUtf8().constData(), op_->currentText().toUtf8().constData(), unit_->currentText().toUtf8().constData(),
           dm_->currentText().toUtf8().constData(), d1_->isVisible() ? d1_->text().toUtf8().constData() : "", d2_->isVisible() ? (" " + d2_->text()).toUtf8().constData() : "", r.toUtf8().constData(), m.toUtf8().constData());
}
bool ToolFilterDialog::testSet(const QString &key, const QString &v) {
    if (key.startsWith("type")) { int ty = key.mid(4).toInt(); for (int k = 0; k < TMF_NTYPES; k++) if (TMF_TYPES[k].type == ty) { types_[k]->setChecked(v.toInt() != 0); return true; } return false; }
    if (key.startsWith("mat")) { int k = key.mid(3).toInt(); if (k < 0 || k > 5) return false; mat_[k]->setChecked(v.toInt() != 0); return true; }
    if (key.startsWith("rad")) { int k = key.mid(3).toInt(); if (k < 0 || k > 2) return false; rad_[k]->setChecked(v.toInt() != 0); return true; }
    if (key == "op" || key == "unit" || key == "dsel") { QComboBox *c = key == "op" ? op_ : key == "unit" ? unit_ : dm_; c->setCurrentIndex(v.toInt()); emit c->activated(v.toInt()); return true; }
    if (key == "d1" || key == "d2") { QLineEdit *e = key == "d1" ? d1_ : d2_; if (!e->isVisible()) return false; e->setText(v); emit e->editingFinished(); return true; }
    if (key == "ok") { ok_->click(); return true; }
    if (key == "cancel") { cancel_->click(); return true; }
    for (QPushButton *p : body()->findChildren<QPushButton *>()) {   /* all / none / matall / matnone / reset: the buttons by their text */
        QString t = p->text(); int x = p->x();
        if ((key == "all" && t == "All" && x < 300) || (key == "none" && t == "None" && x < 300) || (key == "matall" && t == "All" && x > 300) || (key == "matnone" && t == "None" && x > 300) || (key == "reset" && t == "Reset all")) { p->click(); return true; }
    }
    return false;
}

/* ------------------------------------------------------------------------------------------------ ReportWindow */
/* the laid-out report (core_ui tlr: rpx templates, twips) on letter pages (12240 x 15840 twips, 1440 margins, 1/15 px per twip
   at 96 dpi) as mcamui draw_tool_report; Mastercam's ActiveReports viewer itself (toolbar, zoom) is NOT DECODED */
class ReportPages : public QWidget {
public:
    using QWidget::QWidget;
    QSize sizeHint() const override { const float s = 1.0f / 15; return QSize((int)(12240 * s) + 40, (int)(tlr.pages * (15840 * s + 16))); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); const float s = 1.0f / 15; float pw = 12240 * s, ph = 15840 * s, m = 1440 * s, gap = 16;
        p.fillRect(rect(), palette().window());
        QFontMetricsF fm(font());
        for (int pg = 0; pg < tlr.pages; pg++) {
            float x0 = (width() - pw) / 2, y0 = pg * (ph + gap);
            p.fillRect(QRectF(x0, y0, pw, ph), Qt::white); p.setPen(QColor(160, 160, 160)); p.drawRect(QRectF(x0, y0, pw, ph)); p.setPen(Qt::black);
            for (int i = 0; i < tlr.n; i++) {
                const RItem *it = &tlr.it[i]; if (it->page != pg) continue;
                if (it->line == 2) {                  /* an AR.Image's embedded picture; SizeMode 2 = zoom (aspect kept, centred in the box) */
                    QImage im = QImage::fromData(it->img, it->nimg); if (im.isNull()) continue;
                    QRectF box(x0 + m + it->x * s, y0 + m + it->y * s, it->w * s, it->h * s), r = box;
                    if (it->sizemode == 2) { QSizeF sz = QSizeF(im.size()).scaled(box.size(), Qt::KeepAspectRatio); r = QRectF(box.center() - QPointF(sz.width() / 2, sz.height() / 2), sz); }
                    else if (it->sizemode == 0) r = QRectF(box.topLeft(), QSizeF(im.size()) * (s * 15 / 1.0) ).intersected(box);
                    p.drawImage(r, im); continue; }
                if (it->line) {                       /* ActiveReports BorderLineStyle: 2 dash, 3 dot, 4 dash-dot, 5 dash-dot-dot (Qt's patterns) */
                    static const Qt::PenStyle st[6] = { Qt::SolidLine, Qt::SolidLine, Qt::DashLine, Qt::DotLine, Qt::DashDotLine, Qt::DashDotDotLine };
                    p.setPen(QPen(Qt::black, 1, it->ls >= 0 && it->ls < 6 ? st[it->ls] : Qt::SolidLine));
                    p.drawLine(QPointF(x0 + m + it->x * s, y0 + m + it->y * s), QPointF(x0 + m + it->w * s, y0 + m + it->h * s)); p.setPen(Qt::black); continue; }
                if (!it->text || !it->text[0]) continue;
                QString t = QString::fromUtf8(it->text); float tw = fm.horizontalAdvance(t), x = x0 + m + it->x * s, w = it->w * s;
                if (it->align == 2) x += w - tw; else if (it->align == 1) x += (w - tw) / 2;
                p.drawText(QPointF(x, y0 + m + it->y * s + fm.ascent()), t);
            }
        }
    }
};
ReportWindow::ReportWindow(QWidget *parent) : FloatDlg("", parent) {
    setObjectName("toolReport");
    setBodySize(QSize(900, 730));                      /* mcamui: a 900 x 760 window */
    scroll_ = new QScrollArea(body()); scroll_->setGeometry(0, 0, 900, 730); scroll_->setWidgetResizable(false); scroll_->show();
    pages_ = new ReportPages; scroll_->setWidget(pages_);
    connect(this, &FloatDlg::closeClicked, this, [this] { show_tlr = 0; hide(); });
}
void ReportWindow::refresh() { setTitle(QString::fromUtf8(tlr_title)); pages_->resize(pages_->sizeHint().expandedTo(QSize(880, 100))); pages_->update(); }

/* ------------------------------------------------------------------------------------------------ RenumberDialog */
/* opmanres 805 "Renumber tools" (182 x 182 du) at its template positions (formats/renumber_tools.md 1): 25951 text, 1357 /
   1312 Start / Increment, 1406 / 1408 the offset checks (two-line labels, 104 x 18) with the amounts 1114 / 1383 beside them
   (enabled with their box, FUN_1804571e0), 1468 head, 1248 unused, OK / Cancel / Help (greyed) */
RenumberDialog::RenumberDialog(QWidget *parent) : FloatDlg("Renumber tools", parent) {
    setObjectName("renumber");
    setBodySize(du(0, 0, 182, 182).size());
    QWidget *b = body();
    auto *t = mk<QLabel>(b, du(12, 7, 156, 19)); t->setWordWrap(true); t->setText("Tools will be renumbered relative to the current order of your operations.");
    mk<QLabel>(b, du(14, 38, 102, 10))->setText("Starting tool number"); mk<QLabel>(b, du(14, 58, 102, 10))->setText("Tool number increment");
    static const int er[4][2] = { {118,36}, {118,57}, {118,79}, {118,100} };
    for (int k = 0; k < 4; k++) { ed_[k] = mk<QLineEdit>(b, du(er[k][0], er[k][1], 40, 12)); QLineEdit *e = ed_[k];
        connect(e, &QLineEdit::textEdited, this, [this, k, e] { if (!filling_) snprintf(rn_txt[k], 16, "%s", e->text().toUtf8().constData()); }); }
    /* BS_AUTOCHECKBOX | BS_MULTILINE (104 x 18): the box, then its text wrapped in the rest of the rectangle; the one-line ones
       may run to the dialog's edge (the UI font is wider than MS Shell Dlg) */
    auto check = [this, b](int x, int y, int w, int h, const char *txt, int *var) {
        QRect r = du(x, y, w, h); auto *c = mk<QCheckBox>(b);
        if (h > 12) { c->setGeometry(r.x(), r.y(), 18, r.height()); auto *l = mk<QLabel>(b, QRect(r.x() + 18, r.y() - 2, r.width() - 18, r.height() + 4)); l->setWordWrap(true); l->setText(txt);
                      l->setBuddy(c); }
        else { c->setGeometry(r.x(), r.y() - 2, du(0, 0, 182 - x - 2, 0).width(), r.height() + 4); c->setText(txt); }
        connect(c, &QCheckBox::toggled, this, [this, var](bool on) { if (!filling_) { *var = on; refresh(); } });
        return c; };
    len_ = check(14, 77, 104, 18, "Length offset value to be added to tool number", &rn_len_on);
    dia_ = check(14, 98, 104, 18, "Diameter offset value to be added to tool number", &rn_dia_on);
    head_ = check(14, 123, 143, 10, "Renumber head number to tool number", &rn_head);
    unused_ = check(14, 142, 162, 10, "Also renumber tools not used in any operation", &rn_unused);
    ok_ = mk<QPushButton>(b, du(64, 157, 36, 18)); ok_->setText("OK"); cancel_ = mk<QPushButton>(b, du(101, 157, 36, 18)); cancel_->setText("Cancel");
    auto *help = mk<QPushButton>(b, du(138, 157, 36, 18)); help->setText("Help"); help->setEnabled(false);
    connect(ok_, &QPushButton::clicked, this, [this] { auto fn = [] { rn_ok(); }; if (withGL) withGL(fn); else fn(); emit closed(); });
    connect(cancel_, &QPushButton::clicked, this, [this] { rn_cancel(); emit closed(); });
    connect(this, &FloatDlg::closeClicked, this, [this] { rn_cancel(); emit closed(); });
}
void RenumberDialog::refresh() {
    filling_ = true;
    for (int k = 0; k < 4; k++) if (ed_[k]->text() != QString::fromUtf8(rn_txt[k])) ed_[k]->setText(QString::fromUtf8(rn_txt[k]));
    len_->setChecked(rn_len_on); dia_->setChecked(rn_dia_on); head_->setChecked(rn_head); unused_->setChecked(rn_unused);
    ed_[2]->setEnabled(rn_len_on != 0); ed_[3]->setEnabled(rn_dia_on != 0);   /* FUN_1804571e0: the amount follows its box */
    filling_ = false;
}
void RenumberDialog::dump() const {
    printf("    rndlg start \"%s\" inc \"%s\" len %d \"%s\"%s dia %d \"%s\"%s head %d unused %d\n", ed_[0]->text().toUtf8().constData(), ed_[1]->text().toUtf8().constData(),
           len_->isChecked(), ed_[2]->text().toUtf8().constData(), ed_[2]->isEnabled() ? "" : " (grey)", dia_->isChecked(), ed_[3]->text().toUtf8().constData(), ed_[3]->isEnabled() ? "" : " (grey)",
           head_->isChecked(), unused_->isChecked());
}
bool RenumberDialog::testSet(const QString &key, const QString &v) {
    bool okk; int k = key.toInt(&okk);
    if (okk && k >= 0 && k < 4) { if (!ed_[k]->isEnabled()) return false; ed_[k]->setText(v); emit ed_[k]->textEdited(v); return true; }
    QCheckBox *c = key == "lon" ? len_ : key == "don" ? dia_ : key == "head" ? head_ : key == "unused" ? unused_ : nullptr;
    if (c) { c->setChecked(v.toInt() != 0); return true; }
    if (key == "ok") { ok_->click(); return true; }
    if (key == "cancel") { cancel_->click(); return true; }
    return false;
}
