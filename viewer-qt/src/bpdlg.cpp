#include "bpdlg.h"
#include "textfit.h"
#include "icons.h"
#include <QTreeWidget>
#include <QTabBar>
#include <QLineEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QGroupBox>
#include <QComboBox>
#include <QPainter>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QMenu>
#include <QHeaderView>
#include <cmath>
#include <cstdio>

/* dialog units of the backplotres pages -> px as mcamui's BS_DX / BS_DY (MS Shell Dlg 8 at 96 dpi: 6 x 13 base units) */
static const double DX = 1.5, DY = 1.625;
void bp_widget_font(QWidget *w) { QFont f = TextFit::dialogFont(w->font()); w->setFont(f); for (QWidget *c : w->findChildren<QWidget *>()) c->setFont(f); }
QPixmap bp_bitmap(const QString &img, int w, int h) { return Icons::icon(QString("#file:%1.png").arg(img), w, h).pixmap(w, h); }
static QToolButton *bmpButton(QWidget *parent, const QString &img, const QString &tip, QRect r) {
    auto *b = new QToolButton(parent); b->setGeometry(r); b->setIcon(QIcon(bp_bitmap(img, 16, 16))); b->setIconSize(QSize(16, 16));
    if (!tip.isEmpty()) b->setToolTip(tip);
    b->setStyleSheet("QToolButton { background: rgb(240,240,240); border: 1px solid rgb(170,170,170); border-radius: 2px; }"
                     " QToolButton:checked { background: rgb(196,214,236); border-color: rgb(90,130,190); } QToolButton:hover:enabled { background: rgb(228,236,246); }");   /* mcamui bp_btn colours (ours) */
    return b;
}

/* ------------------------------------------------------------------------------------------------ Details / Info pages */
/* backplotres 4000 / 4001 / 4002 / 4006 (core_ui BP_PG*): statics (wrapped), lines, group boxes, bitmaps, the disabled Circle
   check, read-only edits with the values (bp_det_val / bp_info_val); the cycle-time statics hidden when the total is 0 */
void BpPage::paintEvent(QPaintEvent *) {
    if (!bp_on) return;
    QPainter p(this); QFontMetrics fm(font());
    const BpCtl *c; int hide = 0; const char *(*val)(int, char *);
    if (bp_tab == 0) { c = bp_det_page(); val = bp_det_val; } else { hide = bp_info_refresh(); c = BP_PG4006; val = bp_info_val; }
    if (!c) return;
    for (; c->t; c++) {
        if (bp_page_hidden(c->id, hide)) continue;
        QRect r((int)lround(c->x * DX), (int)lround(c->y * DY), (int)lround(c->w * DX), (int)lround(c->h * DY));
        if (r.y() > height() - 4) continue;
        char b[64]; const char *s = c->s;
        if (c->id && !s) s = val(c->id, b);
        switch (c->t) {
        case 's': if (s) { p.setPen(QColor(20, 20, 20)); p.drawText(QRect(r.x(), r.y(), r.width() + 40, qMax(r.height(), fm.height() + 2) + 30), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, QString::fromUtf8(s)); } break;
        case 'l': p.setPen(QColor(160, 160, 160)); p.drawLine(r.x(), r.y(), r.right(), r.y()); p.setPen(Qt::white); p.drawLine(r.x(), r.y() + 1, r.right(), r.y() + 1); break;
        case 'g': p.setPen(QColor(190, 190, 190)); p.drawRect(r.adjusted(0, 6, -1, -1)); if (s && *s) { int tw = fm.horizontalAdvance(s); p.fillRect(r.x() + 6, r.y(), tw + 6, 13, palette().window()); p.setPen(Qt::black); p.drawText(r.x() + 9, r.y() + fm.ascent(), s); } break;
        case 'b': p.drawPixmap(r.x(), r.y(), bp_bitmap(QString::fromUtf8(c->s), 16, 16)); break;   /* SS_BITMAP */
        case 'c': p.setPen(QColor(120, 120, 120)); p.fillRect(QRect(r.x(), r.y() + (r.height() - 13) / 2, 13, 13), QColor(240, 240, 240)); p.drawRect(QRect(r.x(), r.y() + (r.height() - 13) / 2, 12, 12)); break;   /* 4207 Circle: disabled */
        case 'e': p.fillRect(r, QColor(240, 240, 240)); p.setPen(QColor(170, 170, 170)); p.drawRect(r.adjusted(0, 0, -1, -1));
                  if (s) { p.setPen(QColor(20, 20, 20)); p.drawText(r.adjusted(3, 0, -1, 0), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(s)); } break;
        }
    }
}

/* ------------------------------------------------------------------------------------------------ Backplot (14000) */
BackplotDialog::BackplotDialog(QWidget *parent) : FloatDlg("Backplot", parent) {
    setObjectName("backplot");
    QWidget *b = body();
    exp_ = bmpButton(b, "mccoreres_BMP_25009", "Expand or contract this dialog", QRect(0, 0, 21, 19));
    connect(exp_, &QToolButton::clicked, this, [this] { bp_exp = !bp_exp; emit changed(); });
    for (int k = 0; k < 7; k++) {                         /* ToolpathControl toggles (core_ui BP_TOGGLES / bp_tgl) */
        tgl_[k] = bmpButton(b, BP_TOGGLES[k].img, BP_TOGGLES[k].tip, QRect((int)BP_TOGGLES[k].x, 30, 26, 26));
        tgl_[k]->setCheckable(BP_TOGGLES[k].byte >= 0);
        int id = BP_TOGGLES[k].id;
        connect(tgl_[k], &QToolButton::clicked, this, [this, id] { if (filling_) return; bp_tgl(id); emit changed(); });
    }
    static const struct { int id; const char *img, *tip; } R2[5] = {
        { 16010, "backplotres_BMP_29364", "Restrict drawing" }, { 16011, "backplotres_BMP_29365", "Turn off restricted drawing" },
        { 16007, "backplotres_BMP_16007", "Save tool geometry" }, { 16001, "backplotres_BMP_16001", "Save as geometry" }, { 16002, "backplotres_BMP_16002", "Add point" } };
    for (int k = 0; k < 5; k++) {
        row2_[k] = bmpButton(b, R2[k].img, R2[k].tip, QRect());
        row2_[k]->setEnabled(R2[k].id != 16007 && R2[k].id != 16001);   /* Save tool geometry / Save as geometry: NOT DONE */
        int id = R2[k].id;
        connect(row2_[k], &QToolButton::clicked, this, [this, id] { bp_row2(id); emit changed(); });
    }
    ok_ = new QPushButton("OK", b); help_ = new QPushButton("Help", b); help_->setEnabled(false);
    connect(ok_, &QPushButton::clicked, this, [this] { bp_stop(); emit changed(); });
    connect(this, &FloatDlg::closeClicked, this, [this] { bp_stop(); emit changed(); });   /* WM_CLOSE: posts 0x802a (the backplot ends) */
    tree_ = new QTreeWidget(b); tree_->setHeaderHidden(true); tree_->setRootIsDecorated(true); tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it) { if (filling_) return; int r = it->data(0, Qt::UserRole).toInt(); if (r >= 0 && r < rows_.size()) { bp_tree_click(&rows_[r]); emit changed(); } });
    auto tog = [this](QTreeWidgetItem *it) { if (filling_) return; int r = it->data(0, Qt::UserRole).toInt(); if (r >= 0 && r < rows_.size()) { bp_tree_toggle(&rows_[r]); emit changed(); } };
    connect(tree_, &QTreeWidget::itemExpanded, this, tog); connect(tree_, &QTreeWidget::itemCollapsed, this, tog);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](QPoint pt) {   /* menu 19100: Expand All 19101 / Collapse All 19102 */
        QMenu m; QAction *e = m.addAction("Expand All"), *c = m.addAction("Collapse All");
        QAction *a = m.exec(tree_->viewport()->mapToGlobal(pt));
        if (a == e) bp_tree_expand_all(1); else if (a == c) bp_tree_expand_all(0); else return;
        emit changed(); });
    tabs_ = new QTabBar(b); tabs_->addTab("Details"); tabs_->addTab("Info");   /* strings 16005 / 16006 */
    connect(tabs_, &QTabBar::currentChanged, this, [this](int i) { if (filling_) return; bp_tab = i; emit changed(); });
    page_ = new BpPage(b); page_->setAutoFillBackground(true);
    bp_widget_font(this);
    refresh();
}
void BackplotDialog::layoutBody() {
    const int cap = 26, Wc = 252, wh = bp_exp ? (int)fmax(bp_exp_h, 500) : 139 + cap;   /* OnInitDialog FUN_66d15d30: 0x8b + caption; expanded >= 500 */
    int bh = wh - cap;
    if (bh != shownH_) { setBodySize(QSize(Wc, bh)); shownH_ = bh; }
    const int ty = 30, bw = 26, ry = ty + 25, hw = 54, hh = 29, Hc = bh - 8 - 30;   /* FUN_66d19410 (row 2 at y = 25, OK / Help bottom right) */
    row2_[0]->setGeometry(Wc - (4 * bw + 12), ry, bw, bw); row2_[1]->setGeometry(Wc - (3 * bw + 10), ry, bw, bw);
    row2_[2]->setGeometry(Wc - (2 * bw + 8), ry, bw, bw); row2_[3]->setGeometry(Wc - (bw + 4), ry, bw, bw);
    row2_[4]->setGeometry((int)(76 * DX), ry, 24, 21);
    ok_->setGeometry(Wc - 2 * hw - 6, ty + Hc - 1 - hh, hw, hh); help_->setGeometry(Wc - hw - 4, ty + Hc - 1 - hh, hw, hh);
    int sh = Hc - 68 - hh;                                  /* the splitter area: y = 60, height H - 68 - hh; < 1 -> hidden */
    bool on = sh >= 1; tree_->setVisible(on); tabs_->setVisible(on); page_->setVisible(on);
    if (on) { int th = sh / 2;                              /* pane split NOT DECODED: halves */
              tree_->setGeometry(0, ty + 60, Wc, th); int ih = sh - th, tbh = tabs_->sizeHint().height();
              tabs_->setGeometry(2, ty + 60 + th + 2, Wc - 4, tbh); page_->setGeometry(4, ty + 60 + th + 2 + tbh + 2, Wc - 8, ih - tbh - 6); }
}
void BackplotDialog::refresh() {
    if (!bp_on) return;
    filling_ = true;
    exp_->setIcon(QIcon(bp_bitmap(bp_exp ? "mccoreres_BMP_25008" : "mccoreres_BMP_25009", 16, 16)));   /* 0x61b0 expanded / 0x61b1 collapsed */
    for (int k = 0; k < 7; k++) { tgl_[k]->setEnabled(bp_tgl_enabled(BP_TOGGLES[k].id)); if (BP_TOGGLES[k].byte >= 0) tgl_[k]->setChecked(bpl.flags[BP_TOGGLES[k].byte] != 0); }
    layoutBody();
    static BpTreeRow buf[2100]; int nr = bp_tree_rows(buf, 2100);
    bool same = nr == rows_.size();
    for (int i = 0; same && i < nr; i++) same = !memcmp(&buf[i], &rows_[i], sizeof buf[i]);
    if (!same) {
        rows_.clear(); for (int i = 0; i < nr; i++) rows_.push_back(buf[i]);
        tree_->clear(); QTreeWidgetItem *parents[3] = {};
        for (int i = 0; i < nr; i++) {
            const BpTreeRow &r = rows_[i];
            auto *it = r.lvl == 0 || !parents[r.lvl - 1] ? new QTreeWidgetItem(tree_) : new QTreeWidgetItem(parents[r.lvl - 1]);
            it->setText(0, QString::fromUtf8(r.text)); it->setData(0, Qt::UserRole, i);
            if (r.expandable) it->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            parents[r.lvl] = it;
            if (r.selected) it->setSelected(true);
        }
        QTreeWidgetItemIterator x(tree_);
        for (; *x; ++x) { int i = (*x)->data(0, Qt::UserRole).toInt(); (*x)->setExpanded(rows_[i].open != 0); }
    }
    tabs_->setCurrentIndex(bp_tab);
    page_->update();
    filling_ = false;
}
void BackplotDialog::dump() const {
    printf("    bpdlg %dx%d exp %d tab %d toggles", width(), height(), bp_exp, tabs_->currentIndex());
    for (int k = 0; k < 7; k++) printf(" %d%s%s", BP_TOGGLES[k].id, tgl_[k]->isChecked() ? "+" : "", tgl_[k]->isEnabled() ? "" : "(off)");
    printf(" tree %d rows%s\n", (int)rows_.size(), tree_->isVisible() ? "" : " (hidden)");
}
bool BackplotDialog::testSet(const QString &key, const QString &v) {
    if (key == "exp") { exp_->click(); return true; }
    if (key == "tgl") { for (int k = 0; k < 7; k++) if (BP_TOGGLES[k].id == v.toInt() && tgl_[k]->isEnabled()) { tgl_[k]->click(); return true; } return false; }
    if (key == "row2") { static const int ids[5] = { 16010, 16011, 16007, 16001, 16002 }; for (int k = 0; k < 5; k++) if (ids[k] == v.toInt() && row2_[k]->isEnabled()) { row2_[k]->click(); return true; } return false; }
    if (key == "tab") { tabs_->setCurrentIndex(v.toInt()); return true; }
    if (key == "ok") { ok_->click(); return true; }
    if (key == "close") { emit closeClicked(); return true; }
    if (key == "tree") { QTreeWidgetItemIterator x(tree_); for (; *x; ++x) if ((*x)->data(0, Qt::UserRole).toInt() == v.toInt()) { emit tree_->itemClicked(*x, 0); return true; } return false; }
    return false;
}

/* ------------------------------------------------------------------------------------------------ VCR bar (15000) */
/* CreateSlider @66d11570: the client width x 30 px at (0, 1) of the graphics view; buttons 24 x 24 at y = height / 2 - 12
   (WM_SIZE FUN_66d1ff40, core_ui BP_VCR x), Trace / Run / the sliders / Conditional stops / Help only when width > height */
BpVcrBar::BpVcrBar(QWidget *view) : QWidget(view) {
    setObjectName("bpVcr"); setMouseTracking(true); setCursor(Qt::ArrowCursor); setAttribute(Qt::WA_Hover);
}
QRect BpVcrBar::btnRect(int k) const { int x = BP_VCR[k].x >= 0 ? (int)BP_VCR[k].x : width() + (int)BP_VCR[k].x; return QRect(x, 30 / 2 - 12, 24, 24); }
int BpVcrBar::hit(QPoint p) const {
    for (int k = 0; k < BP_NVCR; k++) { if (k >= 6 && width() <= 30) break; if (btnRect(k).contains(p)) return BP_VCR[k].id; }
    return 0;
}
void BpVcrBar::paintEvent(QPaintEvent *) {
    if (!bp_on) return;
    QPainter p(this); p.fillRect(rect(), palette().window());
    for (int k = 0; k < BP_NVCR; k++) {
        if (k >= 6 && width() <= 30) break;
        QRect r = btnRect(k); int id = BP_VCR[k].id; bool chk = bp_vcr_checked(id), en = bp_vcr_enabled(id), hov = en && hover_ == id;
        p.setPen(chk ? QColor(90, 130, 190) : QColor(170, 170, 170)); p.setBrush(chk ? QColor(196, 214, 236) : hov ? QColor(228, 236, 246) : QColor(240, 240, 240));
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
        p.drawPixmap(r.x() + 4, r.y() + 4, bp_bitmap(BP_VCR[k].img, 16, 16));
        if (!en) p.fillRect(r, QColor(240, 240, 240, 150));
    }
    if (width() <= 30) return;
    QRect ps = posRect();
    if (ps.width() > 20) {                                  /* position slider 15001 + the conditional stops' ticks; run speed 15006 */
        p.setPen(Qt::NoPen); p.setBrush(QColor(210, 210, 210)); p.drawRoundedRect(QRect(ps.x(), ps.y() + 10, ps.width(), 6), 2, 2);
        p.setPen(QColor(90, 90, 90));
        for (int k = 0; k < bpl.nstops; k++) { int tx = ps.x() + (int)(bp_stop_tick(k) * ps.width()); p.drawLine(tx, ps.y() + 18, tx, ps.y() + 24); }
        int hx = ps.x() + (int)(bp_slider_pos() * ps.width());
        p.setPen(Qt::NoPen); p.setBrush(QColor(60, 110, 180)); p.drawRoundedRect(QRect(hx - 4, ps.y() + 4, 8, 18), 1, 1);
        QRect sr = speedRect();
        p.setBrush(QColor(210, 210, 210)); p.drawRoundedRect(QRect(sr.x(), sr.y() + 10, sr.width(), 6), 2, 2);
        int sx = sr.x() + bp_speed_pos * sr.width() / 100;
        p.setBrush(QColor(60, 110, 180)); p.drawRoundedRect(QRect(sx - 3, sr.y() + 4, 6, 18), 1, 1);
    }
}
void BpVcrBar::slide(QPoint p) {
    if (drag_ == 1) { QRect ps = posRect(); if (bpl.vis1 > bpl.vis0) bp_slider_seek((double)(p.x() - ps.x()) / ps.width()); }
    else if (drag_ == 2) { QRect sr = speedRect(); double f = (double)(p.x() - sr.x()) / sr.width(); bp_speed_pos = (int)lround((f < 0 ? 0 : f > 1 ? 1 : f) * 100); }
    update(); emit changed();
}
void BpVcrBar::mousePressEvent(QMouseEvent *e) {
    QPoint p = e->position().toPoint();
    if (e->button() != Qt::LeftButton) return;
    press_ = hit(p);
    if (!press_ && width() > 30 && posRect().width() > 20) { if (posRect().contains(p)) drag_ = 1; else if (speedRect().contains(p)) drag_ = 2; if (drag_) slide(p); }
}
void BpVcrBar::mouseMoveEvent(QMouseEvent *e) {
    QPoint p = e->position().toPoint(); int h = hit(p);
    if (h != hover_) { hover_ = h; update(); }
    if (drag_) slide(p);
}
void BpVcrBar::mouseReleaseEvent(QMouseEvent *e) {
    QPoint p = e->position().toPoint();
    if (press_ && hit(p) == press_ && bp_vcr_enabled(press_)) { bp_vcr(press_); emit changed(); }
    press_ = 0; drag_ = 0; update();
}
bool BpVcrBar::event(QEvent *e) {
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e); int id = hit(he->pos());
        for (int k = 0; k < BP_NVCR; k++) if (BP_VCR[k].id == id) { QToolTip::showText(he->globalPos(), BP_VCR[k].tip, this); return true; }
        if (width() > 30 && speedRect().contains(he->pos())) { QToolTip::showText(he->globalPos(), "Run speed", this); return true; }
        QToolTip::hideText(); return true;
    }
    if (e->type() == QEvent::Leave) { hover_ = 0; update(); }
    return QWidget::event(e);
}

/* ------------------------------------------------------------------------------------------------ Options (1228) */
/* the controls of core_ui BPO_CTLS at their template positions (chainDu: the 6 x 13 base units); bindings, enable rules,
   clicks and edit commits are the core's (bpo_on / bpo_en / bpo_click / bpo_val / bpo_edit_commit); a rejected MCByte shows
   the ClueWnd text as a tooltip under the edit */
BpOptionsDialog::BpOptionsDialog(QWidget *parent) : FloatDlg("Backplot Options", parent) {
    setObjectName("bpOptions");
    setBodySize(chainDu(this, 0, 0, 413, 369).size());
    QWidget *b = body();
    for (const BpoCtl *c = BPO_CTLS; c->t; c++) {
        QRect r = chainDu(this, (int)c->x, (int)c->y, (int)c->w, (int)c->h);
        Ctl k = { c, nullptr, nullptr, nullptr, nullptr };
        switch (c->t) {
        case 'g': { auto *g = new QGroupBox(c->text, b); g->setGeometry(r); g->lower(); k.w = g; break; }
        case 's': { auto *l = new QLabel(c->text, b); l->setWordWrap(true); l->setAlignment(Qt::AlignLeft | Qt::AlignTop); l->setGeometry(r.adjusted(0, 0, 0, 4)); k.w = l; break; }
        case 'i': { auto *l = new QLabel(b); l->setPixmap(bp_bitmap(c->text, r.width(), r.height())); l->setGeometry(r); k.w = l; break; }
        case 'r': case 'c': {
            QAbstractButton *x = c->t == 'r' ? (QAbstractButton *)new QRadioButton(c->text, b) : (QAbstractButton *)new QCheckBox(c->text, b);
            x->setAutoExclusive(false); x->setGeometry(r.adjusted(0, 0, x->sizeHint().width() > r.width() ? x->sizeHint().width() - r.width() : 0, 0));
            int key = c->key; connect(x, &QAbstractButton::clicked, this, [this, key] { if (filling_) return; if (bpo_en(key)) bpo_click(key); emit changed(); });
            k.w = x; break; }
        case 'e': case 'k': {
            QRect er = c->t == 'k' ? chainDu(this, (int)c->x, (int)c->y, 20, 14) : r;
            auto *e = new QLineEdit(b); e->setGeometry(er); k.w = e;
            int key = c->key;
            connect(e, &QLineEdit::editingFinished, this, [this, e, key] {
                if (filling_ || !bpo_en(key)) return;
                int kd; double lo, hi; void *v = bpo_val(key, &kd, &lo, &hi); char clue[32] = "";
                if (v && bpo_edit_commit(kd, v, e->text().toUtf8().constData(), lo, hi, clue, sizeof clue)) QToolTip::showText(e->mapToGlobal(QPoint(0, e->height() + 2)), clue, e);
                emit changed(); });
            if (c->t == 'k') {                              /* the swatch (owner-draw FUN_1807055f0) + Select (bitmap 0x61d2 -> the Colors picker) */
                k.swatch = new QLabel(b); k.swatch->setGeometry(chainDu(this, (int)c->x2, (int)c->y, 20, 14)); k.swatch->setFrameShape(QFrame::Box);
                k.sel = bmpButton(b, "mccoreres_BMP_25042", QString(), chainDu(this, (int)c->x3, (int)c->y, 16, 14));
                connect(k.sel, &QToolButton::clicked, this, [this, key] { bpo_select(key); emit changed(); });
            }
            break; }
        case 'm': { auto *m = new QComboBox(b); m->setGeometry(r); k.w = m; break; }   /* material list: empty (names NOT DECODED) */
        case 'p': {                                         /* up-down 359 / 363: range 1..100, writes the global */
            auto *up = new QToolButton(b); up->setArrowType(Qt::UpArrow); up->setGeometry(r.x(), r.y(), r.width(), r.height() / 2);
            auto *dn = new QToolButton(b); dn->setArrowType(Qt::DownArrow); dn->setGeometry(r.x(), r.y() + r.height() / 2, r.width(), r.height() - r.height() / 2);
            int key = c->key;
            auto spin = [this, key](int u) { int kd; double lo, hi; bpo_spin((double *)bpo_val(key, &kd, &lo, &hi), u); emit changed(); };
            connect(up, &QToolButton::clicked, this, [spin] { spin(1); }); connect(dn, &QToolButton::clicked, this, [spin] { spin(0); });
            k.w = up; k.dn = dn; break; }
        case 'b': { auto *p = new QPushButton(c->text, b); p->setGeometry(r); int key = c->key;
                    connect(p, &QPushButton::clicked, this, [this, key] { if (bpo_en(key)) bpo_click(key); emit changed(); }); k.w = p; break; }
        }
        ctls_.push_back(k);
    }
    connect(this, &FloatDlg::closeClicked, this, [this] { bpo_cancel(); emit changed(); });   /* the title bar X = Cancel */
    bp_widget_font(this);
    refresh();
}
void BpOptionsDialog::refresh() {
    filling_ = true;
    for (Ctl &k : ctls_) {
        const BpoCtl *c = k.c; int en = bpo_en(c->key);
        if (!k.w) continue;
        switch (c->t) {
        case 's': k.w->setEnabled(en); break;
        case 'r': case 'c': { auto *x = static_cast<QAbstractButton *>(k.w); x->setChecked(bpo_on(c->key) != 0); x->setEnabled(en); break; }
        case 'e': case 'k': {
            auto *e = static_cast<QLineEdit *>(k.w); int kd; double lo, hi; void *v = bpo_val(c->key, &kd, &lo, &hi);
            if (v && !e->hasFocus()) { char s[32]; bpo_edit_text(kd, v, s, sizeof s); e->setText(QString::fromUtf8(s)); }
            e->setReadOnly(!en); e->setEnabled(en);
            if (k.swatch) { float rgb[3]; mc_col(en ? *(int *)v : 7, rgb);   /* disabled -> index 7 */
                            k.swatch->setStyleSheet(QString("background: rgb(%1,%2,%3); border: 1px solid rgb(120,120,120);").arg((int)(rgb[0] * 255)).arg((int)(rgb[1] * 255)).arg((int)(rgb[2] * 255))); }
            if (k.sel) k.sel->setEnabled(en);
            break; }
        case 'm': k.w->setEnabled(en); break;
        case 'b': k.w->setEnabled(en); break;
        }
    }
    filling_ = false;
}
void BpOptionsDialog::dump() const {
    printf("    bpodlg:");
    for (const Ctl &k : ctls_) {
        const BpoCtl *c = k.c;
        if (c->t == 'r' || c->t == 'c') printf(" %d%s%s", c->key, static_cast<QAbstractButton *>(k.w)->isChecked() ? "+" : "", k.w->isEnabled() ? "" : "(off)");
        else if (c->t == 'e' || c->t == 'k') printf(" %d=%s%s", c->key, static_cast<QLineEdit *>(k.w)->text().toUtf8().constData(), k.w->isEnabled() ? "" : "(off)");
    }
    printf("\n");
}
bool BpOptionsDialog::testSet(const QString &key, const QString &v) {
    int kk = key.toInt();
    for (Ctl &k : ctls_) {
        if (k.c->key != kk || !k.w || !k.w->isEnabled()) continue;
        if (k.c->t == 'r' || k.c->t == 'c' || k.c->t == 'b') { static_cast<QAbstractButton *>(k.w)->click(); return true; }
        if (k.c->t == 'e' || k.c->t == 'k') { auto *e = static_cast<QLineEdit *>(k.w); e->setFocus(); e->setText(v); emit e->editingFinished(); e->clearFocus(); return true; }
        if (k.c->t == 'p') { (v == "up" ? static_cast<QToolButton *>(k.w) : k.dn)->click(); return true; }
    }
    if (key == "select") { for (Ctl &k : ctls_) if (k.sel && k.c->key == v.toInt() && k.sel->isEnabled()) { k.sel->click(); return true; } }
    return false;
}

/* ------------------------------------------------------------------------------------------------ Conditional Stops (6000) */
/* BackplotDisplayStop: Stop at Operations 6001 (forces toolchange on), Stop at Toolchange 6013, Stop at step number 6002 /
   6003, Stop at XYZ value 6004-6009, Stop at XYZ change 6010-6012, OK = bp_compute_stops, Cancel, Help (greyed); mcamui's rows
   (179 x 162 du, x 1.5 / 1.625) */
BpStopsDialog::BpStopsDialog(QWidget *parent) : FloatDlg("Conditional Stops", parent) {
    setObjectName("bpStops");
    setBodySize(QSize((int)(179 * 1.5), (int)(162 * 1.625 + 4)));
    QWidget *b = body(); int y = 4;
    ops_ = new QCheckBox("Stop at Operations", b); ops_->setGeometry(6, y, 240, 20); y += 22;
    tc_ = new QCheckBox("Stop at Toolchange", b); tc_->setGeometry(6, y, 240, 20); y += 22;
    step_ = new QCheckBox("Stop at step number:", b); step_->setGeometry(6, y, 150, 22); txt_[0] = new QLineEdit(b); txt_[0]->setGeometry(158, y, 70, 22); y += 26;
    auto *l1 = new QLabel("Stop at XYZ value", b); l1->setGeometry(6, y, 120, 18); auto *l2 = new QLabel("Stop at XYZ change", b); l2->setGeometry(134, y, 120, 18); y += 20;
    static const char *ax[3] = { "X:", "Y:", "Z:" }, *axc[3] = { "X", "Y", "Z" };
    for (int k = 0; k < 3; k++) {
        at_[k] = new QCheckBox(ax[k], b); at_[k]->setGeometry(6, y, 50, 22); txt_[1 + k] = new QLineEdit(b); txt_[1 + k]->setGeometry(58, y, 70, 22);
        ch_[k] = new QCheckBox(axc[k], b); ch_[k]->setGeometry(134, y, 80, 22); y += 26;
    }
    auto *ok = new QPushButton("OK", b), *cancel = new QPushButton("Cancel", b), *help = new QPushButton("Help", b);
    ok->setGeometry(6, y + 4, 80, 26); cancel->setGeometry(92, y + 4, 80, 26); help->setGeometry(178, y + 4, 80, 26); help->setEnabled(false);
    auto sync = [this] {
        if (filling_) return; BpStops *cs = &bpl.cs;
        bp_cs_at_ops(ops_->isChecked()); if (!cs->at_ops) cs->at_toolchange = tc_->isChecked();
        cs->at_step = step_->isChecked();
        int *at[3] = { &cs->at_x, &cs->at_y, &cs->at_z }, *ch[3] = { &cs->chg_x, &cs->chg_y, &cs->chg_z };
        for (int k = 0; k < 3; k++) { *at[k] = at_[k]->isChecked(); *ch[k] = ch_[k]->isChecked(); }
        refresh(); };
    for (QCheckBox *c : { ops_, tc_, step_, at_[0], at_[1], at_[2], ch_[0], ch_[1], ch_[2] }) connect(c, &QCheckBox::clicked, this, sync);
    for (int k = 0; k < 4; k++) connect(txt_[k], &QLineEdit::textEdited, this, [k](const QString &t) { snprintf(bp_cs_txt[k], 32, "%s", t.toUtf8().constData()); });
    connect(ok, &QPushButton::clicked, this, [this] { bp_cs_ok(); emit changed(); });
    connect(cancel, &QPushButton::clicked, this, [this] { bp_show_cs = 0; emit changed(); });
    connect(this, &FloatDlg::closeClicked, this, [this] { bp_show_cs = 0; emit changed(); });
    bp_widget_font(this);
}
void BpStopsDialog::refresh() {
    filling_ = true; const BpStops *cs = &bpl.cs;
    ops_->setChecked(cs->at_ops); tc_->setChecked(cs->at_toolchange); step_->setChecked(cs->at_step);
    const int at[3] = { cs->at_x, cs->at_y, cs->at_z }, ch[3] = { cs->chg_x, cs->chg_y, cs->chg_z };
    for (int k = 0; k < 3; k++) { at_[k]->setChecked(at[k]); ch_[k]->setChecked(ch[k]); }
    for (int k = 0; k < 4; k++) if (!txt_[k]->hasFocus()) txt_[k]->setText(QString::fromUtf8(bp_cs_txt[k]));
    filling_ = false;
}
