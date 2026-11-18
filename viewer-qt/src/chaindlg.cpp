#include "retire.h"
#include "textfit.h"
#include "chaindlg.h"
#include "icons.h"
#include <QWindow>
#include <QPainter>
#include <QMouseEvent>
#include <QAbstractButton>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QGroupBox>
#include <QToolTip>
#include <QHelpEvent>
#include <QFontMetrics>
#include <cmath>
#include <cstdio>
#include <cstdlib>

/* dialog units -> pixels as Windows' MapDialogRect does for a DS_SETFONT dialog: x * the base unit width / 4, y * the
   base unit height / 8. The templates' font is MS Shell Dlg 8, whose base units at 96 dpi are 6 x 13 px (mcamui BS_DX 1.5 /
   BS_DY 1.625, the same values); Qt's measured line height for the UI font (Noto Sans 13 px: 18 px with its line gap)
   would make the 428-unit expanded Chaining dialog taller than the window, so these dialogs use the template's units */
/* the templates' font itself: MS Shell Dlg 8 pt = 11 px at 96 dpi (the UI font's family, which the base units above fit) */
static void dlg_font(QWidget *w) {                       /* the window and its controls (the ribbon theme's style sheet stops the inheritance) */
    QFont f = TextFit::dialogFont(w->font()); w->setFont(f);
    for (QWidget *c : w->findChildren<QWidget *>()) c->setFont(f);
}
QRect chainDu(const QWidget *, int x, int y, int wd, int h) {
    const double bx = 6, by = 13;
    return QRect((int)lround(x * bx / 4), (int)lround(y * by / 8), (int)lround(wd * bx / 4), (int)lround(h * by / 8));
}

/* ------------------------------------------------------------------------------------------------ FloatDlg */
/* mcamui's chaining windows are nuklear windows inside the main window (NK_WINDOW_BORDER | TITLE | MOVABLE | CLOSABLE):
   here a tool window of the main window with a drawn title strip, so the UIVertDialog title-bar icon can be clicked */
FloatDlg::FloatDlg(const QString &title, QWidget *parent) : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint), title_(title) {
    setWindowTitle(title);
    setObjectName("floatDlg");
    setAttribute(Qt::WA_ShowWithoutActivating);         /* the graphics view keeps the keyboard (Esc / Enter) */
    dlg_font(this);
    body_ = new QWidget(this);
    body_->move(1, titleH());
    setMouseTracking(true);
}
int FloatDlg::titleH() const { return QFontMetrics(font()).height() + 10; }
void FloatDlg::setTitleIcon(const QString &icon, const QString &tip) { icon_ = icon; iconTip_ = tip; update(); }
void FloatDlg::setBodySize(QSize s) { body_->setGeometry(1, titleH(), s.width(), s.height()); setFixedSize(s.width() + 2, s.height() + titleH() + 4); }
QRect FloatDlg::iconRect() const { int h = titleH(); return QRect(6, (h - 16) / 2, 16, 16); }
QRect FloatDlg::closeRect() const { int h = titleH(); return QRect(width() - h + 4, 4, h - 8, h - 8); }
void FloatDlg::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    int h = titleH();
    p.fillRect(QRect(0, 0, width(), h), palette().window().color().darker(108));
    p.setPen(QColor(150, 156, 165)); p.drawRect(rect().adjusted(0, 0, -1, -1)); p.drawLine(0, h - 1, width(), h - 1);
    int tx = 8;
    if (!icon_.isEmpty()) {                             /* the title-bar icon (no icon pack image: an outline, as mcamui) */
        QRect ir = iconRect();
        if (Icons::hasImage(icon_)) p.drawPixmap(ir, Icons::icon(icon_, 16, 16).pixmap(16, 16));
        else { p.setPen(QColor(90, 90, 90)); p.drawRect(ir.adjusted(0, 0, -1, -1)); }
        tx = ir.right() + 8;
    }
    p.setPen(palette().windowText().color());
    p.drawText(QRect(tx, 0, width() - tx - h, h), Qt::AlignLeft | Qt::AlignVCenter, title_);
    if (!closable_) return;
    QRect cr = closeRect();                             /* the close box */
    p.setPen(QPen(QColor(90, 90, 90), 1.5)); p.drawLine(cr.topLeft() + QPoint(3, 3), cr.bottomRight() - QPoint(3, 3)); p.drawLine(cr.topRight() + QPoint(-3, 3), cr.bottomLeft() + QPoint(3, -3));
}
void FloatDlg::mousePressEvent(QMouseEvent *e) {
    QPoint p = e->position().toPoint();
    if (e->button() != Qt::LeftButton || p.y() >= titleH()) return;
    if (!icon_.isEmpty() && iconRect().contains(p)) return;          /* taken on release */
    if (closable_ && closeRect().contains(p)) return;
    /* the compositor moves the window (Wayland ignores a client's move() of its own top-level window); the manual drag
       below stays for platforms without a system move */
    if (windowHandle() && windowHandle()->startSystemMove()) return;
    dragging_ = true; dragOff_ = e->globalPosition().toPoint() - frameGeometry().topLeft();
}
void FloatDlg::mouseMoveEvent(QMouseEvent *e) { if (dragging_) move(e->globalPosition().toPoint() - dragOff_); }
void FloatDlg::mouseReleaseEvent(QMouseEvent *e) {
    QPoint p = e->position().toPoint();
    if (dragging_) { dragging_ = false; return; }
    if (!icon_.isEmpty() && iconRect().contains(p)) { emit iconClicked(); return; }   /* OnSysCommand: toggles (358) */
    if (closable_ && closeRect().contains(p)) emit closeClicked();
}
bool FloatDlg::event(QEvent *e) {
    if (e->type() == QEvent::ToolTip && !icon_.isEmpty()) {          /* hover tooltip m_expand / m_collapse */
        auto *he = static_cast<QHelpEvent *>(e);
        if (iconRect().contains(he->pos())) { QToolTip::showText(he->globalPos(), iconTip_, this); return true; }
    }
    return QWidget::event(e);
}

/* ------------------------------------------------------------------------------------------------ the buttons */
/* the Chaining dialog's CMFCButtons as mcamui draws them (chain_dlg_body): fill rgb(196,222,246) pressed (a mode / toggle
   that is on), rgb(229,241,251) hovered when live, else rgb(240,240,240); border rgb(86,157,229) pressed, else
   rgb(173,173,173); the mccoreres bitmap inside (2 px in, aspect kept, faded when not live) or the caption; tooltip = the
   template's text (these colours are this program's: the BCGP visual manager's are NOT DECODED) */
class ChainButton : public QAbstractButton {
public:
    ChainButton(const ChCtl *c, QWidget *parent) : QAbstractButton(parent), c_(c) { setToolTip(QString::fromUtf8(c->text)); setMouseTracking(true); setAttribute(Qt::WA_Hover); }
    bool on = false;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        bool live = isEnabled(), hov = underMouse();
        QRectF b = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(on ? QColor(86, 157, 229) : QColor(173, 173, 173));
        p.setBrush(on ? QColor(196, 222, 246) : hov && live ? QColor(229, 241, 251) : QColor(240, 240, 240));
        p.drawRoundedRect(b, 2, 2);
        if (c_->bmp > 0) {
            QString nm = QString("#file:mccoreres_BMP_%1.png").arg(c_->bmp);
            QRect ir = rect().adjusted(2, 2, -2, -2);
            QPixmap pm = Icons::icon(nm, ir.width(), ir.height()).pixmap(ir.size(), live ? QIcon::Normal : QIcon::Disabled);
            if (!pm.isNull()) { QSize s = pm.size().scaled(ir.size(), Qt::KeepAspectRatio); p.drawPixmap(QRect(ir.x() + (ir.width() - s.width()) / 2, ir.y() + (ir.height() - s.height()) / 2, s.width(), s.height()), pm); }
        } else {
            p.setPen(live ? QColor(30, 30, 30) : QColor(160, 160, 160));
            p.drawText(rect(), Qt::AlignCenter, QString::fromUtf8(c_->text));
        }
    }
private:
    const ChCtl *c_;
};

/* ------------------------------------------------------------------------------------------------ Chaining (6464 / 1058) */
/* mccore CChainDlg: core_ui cui_chain.c has the templates (CH_6464 collapsed / CH_1058 expanded, with every citation),
   the handlers (chain_act), FUN_1801f00d0's enabling (chain_ctl_live) and the pressed states (chain_ctl_on). The title
   icon = UIVertDialog's (uictrls UpdateIcon: icon 7021 collapsed / 7022 expanded); a click toggles (358), the tooltip is
   opmanres 7020 "Expand dialog" / 7021 "Contract dialog". */
ChainDialog::ChainDialog(QWidget *parent) : FloatDlg("Chaining", parent) {
    connect(this, &FloatDlg::iconClicked, this, [this] { emit act(358); });
    rebuild();
}
void ChainDialog::rebuild() {
    for (auto &k : ctls_) retireWidget(k.w);           /* retire.h */
    ctls_.clear();
    exp_ = chain_dlg_exp != 0;
    int n, h; const ChCtl *tab = chain_dlg_template(&n, &h); tmpl_ = tab;
    if (chain_dlg_icon()) setTitleIcon(exp_ ? "#file:uictrls_ICO_7022.png" : "#file:uictrls_ICO_7021.png", exp_ ? "Contract dialog" : "Expand dialog");
    else setTitleIcon(QString(), QString());             /* UIVertDialog::ShowExpandButton(+0x17f8 == 0): none in Solids mode (6518) */
    setBodySize(chainDu(this, 0, 0, 143, h).size());     /* FUN_1801f06d0 re-creates the dialog from 1058 / 6464 */
    QWidget *b = body();
    for (int i = 0; i < n; i++) {                        /* group boxes first: the controls inside draw over them */
        const ChCtl *k = &tab[i];
        if (k->bmp != -3) continue;
        auto *g = new QGroupBox(QString::fromUtf8(k->text), b); g->setGeometry(chainDu(this, k->x, k->y, k->w, k->h)); g->show();
        ctls_.push_back({ k, g });
    }
    for (int i = 0; i < n; i++) {
        const ChCtl *k = &tab[i]; QWidget *w = nullptr;
        if (k->bmp == -3) continue;
        int id = k->id;
        if (k->bmp == -2) {                              /* combo 538: the Window / Polygon selection mode */
            auto *c = new QComboBox(b); for (int m = 0; m < 5; m++) c->addItem(QString::fromUtf8(CH_WIN_MODES[m]));
            connect(c, QOverload<int>::of(&QComboBox::activated), this, [](int ix) { win_sel_mode = ix + 1; });
            w = c;
        } else if (k->bmp == -4) {                       /* Wait: a check box */
            auto *c = new QCheckBox(QString::fromUtf8(k->text), b);
            connect(c, &QCheckBox::clicked, this, [this, c](bool v) { if ((int)v != chain_wait) emit act(540); else c->setChecked(chain_wait); });
            w = c;
        } else if (k->bmp == -1) {                       /* C-plane / 3D radio group (DDX_Radio 1482, +0x17e0 = !chain_plane_filter); Solids mode Face 3041 / 3D 1499 (+0x17e4) */
            auto *r = new QRadioButton(QString::fromUtf8(k->text), b); r->setAutoExclusive(false);
            connect(r, &QRadioButton::clicked, this, [this, id] { int on = chain_radio_on(id); if (!on) emit act(id); else refresh(); });
            w = r;
        } else {
            auto *bt = new ChainButton(k, b);
            connect(bt, &QAbstractButton::clicked, this, [this, id] { emit act(id); });
            w = bt;
        }
        QRect g = chainDu(this, k->x, k->y, k->w, k->h);
        if (k->bmp == -1 || k->bmp == -4) g.setWidth(qMax(g.width(), w->sizeHint().width()));   /* the Qt indicator + text may be wider than the template's box: not clipped */
        w->setGeometry(g); w->show();
        ctls_.push_back({ k, w });
    }
    built_ = true;
    dlg_font(this);
    refresh();
}
void ChainDialog::refresh() {
    if ((chain_dlg_exp != 0) != exp_ || chain_dlg_template(nullptr, nullptr) != tmpl_) { rebuild(); return; }   /* 1058 / 6464 / 6518 */
    for (auto &k : ctls_) {
        int id = k.c->id;
        if (k.c->bmp == -3) continue;
        if (k.c->bmp == -2) { auto *c = static_cast<QComboBox *>(k.w); c->setCurrentIndex(win_sel_mode >= 1 && win_sel_mode <= 5 ? win_sel_mode - 1 : 0); continue; }
        k.w->setEnabled(chain_ctl_live(id) != 0);
        if (k.c->bmp == -4) { static_cast<QCheckBox *>(k.w)->setChecked(chain_wait != 0); continue; }
        if (k.c->bmp == -1) { static_cast<QRadioButton *>(k.w)->setChecked(chain_radio_on(id) != 0); continue; }
        auto *bt = static_cast<ChainButton *>(k.w); bt->on = chain_ctl_on(id) != 0; bt->update();
    }
}
bool ChainDialog::testClick(int id) {
    if (id == 358) { emit iconClicked(); return true; }
    for (auto &k : ctls_) if (k.c->id == id && k.c->bmp != -3 && k.c->bmp != -2) {
        if (!k.w->isEnabled()) return false;
        if (auto *ab = qobject_cast<QAbstractButton *>(k.w)) { ab->click(); return true; }
    }
    return false;
}
void ChainDialog::dump() const {
    printf("    chaining dialog %s %dx%d:", tmpl_ == CH_6518 ? "6518" : exp_ ? "1058" : "6464", width(), height());
    for (auto &k : ctls_) {
        if (k.c->bmp == -3) { printf(" [%s]", k.c->text); continue; }
        if (k.c->bmp == -2) { printf(" %d=%s", k.c->id, static_cast<QComboBox *>(k.w)->currentText().toUtf8().constData()); continue; }
        bool on = k.c->bmp == -4 || k.c->bmp == -1 ? static_cast<QAbstractButton *>(k.w)->isChecked() : static_cast<ChainButton *>(k.w)->on;
        printf(" %d%s%s", k.c->id, k.w->isEnabled() ? "" : "-", on ? "*" : "");
    }
    printf("\n");
}

/* ------------------------------------------------------------------------------------------------ Pick Reference Face (134) */
/* mcgeomsldres dialog 134 (118 x 49 dialog units), SRefFaceDlg (core_ui cui_sldchain.c SREF_134, with the citations):
   273 Other face (FUN_18005d690 -> sld_ref_other), OK (sld_ref_ok), Help greyed (Mastercam's help file). Modeless
   (CBCGPDialog::Create) with the Chaining dialog hidden (show_chain_dlg('\0'), FUN_1800a0dd0); its position is NOT
   DECODED: where the Chaining dialog stands. No close box (the template has no system menu style in the export). */
RefFaceDialog::RefFaceDialog(QWidget *parent) : FloatDlg("Pick Reference Face", parent) {
    setClosable(false);
    setBodySize(chainDu(this, 0, 0, 118, 49).size());
    for (int i = 0; i < SREF_134_N; i++) {
        const ChCtl *k = &SREF_134[i]; int id = k->id;
        auto *p = new QPushButton(QString::fromUtf8(k->text), body()); p->setGeometry(chainDu(this, k->x, k->y, k->w, k->h));
        p->setEnabled(id != 9);
        connect(p, &QAbstractButton::clicked, this, [this, id] { emit act(id); });
        b_.push_back({ id, p });
    }
    dlg_font(this);
}
bool RefFaceDialog::testClick(int id) { for (auto &x : b_) if (x.first == id && x.second->isEnabled()) { x.second->click(); return true; } return false; }

/* ------------------------------------------------------------------------------------------------ Drill Point Selection (2300) */
/* mcmillres dialog 2300 (152 x 434 dialog units), the CDrillPtSelection dialog that mcmill tp_pt_mgr (decomp/mcmill_all.c
   @18007a6c0) runs during point picking (FUN_18008b0d0: get_event with mask 0x200 and an empty prompt until OK; the
   picked points are drawn in colour 0xe, FUN_18006c980(op, 0xe), and back to the background colour after). Controls
   and texts from the template; DDX (FUN_18008b700): Mask on Arc diameter 0x175 / tolerance 0x177, the Pattern edits
   0x15d .. 0x16e. Live here: the manual picks (2954, the default: each pick adds its point - an arc gives its centre),
   Unselect (3244, the last point), Unselect all (3245), OK. Greyed (not ported): Automatic, Entities, Window Points,
   Mask on Arc, Subprograms..., Last, Sorting..., Edit..., Pattern / Create points, Grid, Bolt Circle and their edits,
   Help. The manual button's image (2954 has no caption) is NOT DECODED. (mcamui chaindlg_ui.inc drill_sel_body) */
DrillDialog::DrillDialog(QWidget *parent) : FloatDlg("Drill Point Selection", parent) {
    setBodySize(chainDu(this, 0, 0, 152, 434).size());
    QWidget *b = body();
    auto R = [this](int x, int y, int w, int h) { return chainDu(this, x, y, w, h); };
    auto group = [&](int x, int y, int w, int h) { auto *g = new QGroupBox(b); g->setGeometry(R(x, y, w, h)); return g; };
    auto btn = [&](int x, int y, int w, int h, const char *t, bool live) { auto *p = new QPushButton(t, b); p->setGeometry(R(x, y, w, h)); p->setEnabled(live); return p; };
    auto stat = [&](int x, int y, int w, int h, const char *t) { auto *l = new QLabel(t, b); l->setGeometry(R(x, y, w, h)); l->setEnabled(false); };
    auto edit = [&](int x, int y, int w, int h) { auto *e = new QLineEdit(b); e->setGeometry(R(x, y, w, h)); e->setEnabled(false); };
    auto radio = [&](int x, int y, int w, int h, const char *t, bool on) { auto *r = new QRadioButton(t, b); r->setGeometry(R(x, y, w, h)); r->setAutoExclusive(false); r->setChecked(on); r->setEnabled(false); };
    group(14, 78, 121, 69); group(14, 206, 121, 194);
    { auto *m = btn(42, 7, 60, 14, " ", true); m->setCheckable(true); m->setChecked(true); }   /* 2954: manual picking (active) */
    btn(42, 25, 60, 14, "Automatic", false); btn(42, 43, 60, 14, "Entities", false); btn(42, 62, 60, 14, "Window Points", false);
    btn(42, 88, 60, 14, "Mask on Arc", false);
    stat(28, 112, 45, 8, "Diameter:"); edit(78, 109, 40, 14); stat(28, 130, 45, 8, "Tolerance:"); edit(78, 127, 40, 14);
    btn(15, 153, 60, 14, "Subprograms...", false); btn(78, 153, 60, 14, "Last", false);
    btn(15, 170, 60, 14, "Sorting...", false); btn(78, 170, 60, 14, "Edit...", false);
    un_ = btn(15, 187, 60, 14, "Unselect", false); unall_ = btn(78, 187, 60, 14, "Unselect all", false);
    radio(19, 215, 48, 10, "Pattern", true); radio(73, 215, 52, 10, "Create points", false);
    radio(19, 233, 48, 10, "Grid", true); radio(72, 233, 61, 10, "Bolt Circle", false);
    edit(91, 255, 20, 18);
    stat(63, 281, 12, 8, "#"); stat(84, 280, 38, 8, "Distance");
    stat(42, 296, 12, 8, "X"); edit(58, 293, 20, 14); edit(82, 293, 40, 14);
    stat(42, 314, 12, 8, "Y"); edit(58, 311, 20, 14); edit(82, 311, 40, 14);
    stat(18, 332, 60, 8, "Radius:"); edit(82, 329, 40, 14); stat(18, 350, 60, 8, "Start angle:"); edit(82, 346, 40, 14);
    stat(18, 367, 60, 8, "Angle between:"); edit(82, 363, 40, 14); stat(18, 384, 60, 8, "# of holes:"); edit(82, 381, 40, 14);
    ok_ = btn(65, 409, 36, 18, "OK", false); btn(105, 409, 36, 18, "Help", false);
    connect(un_, &QAbstractButton::clicked, this, [this] { drill_unselect(); emit changed(); });
    connect(unall_, &QAbstractButton::clicked, this, [this] { drill_unselect_all(); emit changed(); });
    connect(ok_, &QAbstractButton::clicked, this, [this] { emit ok(); });
    dlg_font(this);
    refresh();
}
void DrillDialog::refresh() { un_->setEnabled(ncholes > 0); unall_->setEnabled(ncholes > 0); ok_->setEnabled(ncholes > 0); }

/* ------------------------------------------------------------------------------------------------ Chaining Options (405) */
/* mccoreres dialog 405 (143 x 403 dialog units), mccore CChainOptions (decomp/mccore_chain_feat_opts.c;
   set_chain_options 0x1801fab10 -> FUN_1801fa9a0: ctor FUN_1801f8a60, init FUN_1801f9a20, DDX FUN_1801f8ef0, OK
   FUN_1801f9680, message map @0x180d2a110), laid out from its template. Session only - the config file is not written.
   Controls (DDX): Mask - 2469 Entity check (chain_use_ent_mask, 6025), 1893 Color (chain_color_filter), 2461 Level
   (chain_level_filter), 2471 / 2473 Closed / Open chains (6013 / 6012), 547 Section angle check (chain_section_stop_angle_on
   6017) + edit 407 (MCReal 0..180 degrees, 6011; enabled by 547, FUN_1801f9870); 2466 Ignore depths (chain_ignore_depths
   6003), 2468 Start chain at point (6009) and 2520 Break closest entity to thread point (6010) exclude each other
   (FUN_1801f98f0 / FUN_1801f9230), 2475 Allow surface edges in Single mode (6026; with it Single mode picks surfaces:
   formats/chaining.md "Surfaces in the Chaining dialog"), 702 Break entities in Dynamic (6007);
   Closed chains: radio 2496 CW / 2501 CCW (chain_direction, 6001) + check 2493 Use cursor position (6002); Open chains:
   radio 2508 One way / 2510 Zigzag (6000); Nested chains: combo 2901 Sorting (mccoreres 5132-5136, nested_chain_sort_mode
   6006), 2512 Infinite nesting (6004), 2536 Reverse inner chains (6005); combo 2447 Sync mode (mccoreres 4200-4206,
   g_chain_sync_mode 6008, > 6 -> 0); edits 2350 Chaining tolerance (MCReal tol .. 1e38, chain_tol) and 2359 Plane
   tolerance (MCReal tol .. 1e38, chain_plane_tol 1002). The Entity / Color / Level buttons (596 / 25114 / 25261, enabled
   by their checks, FUN_1801f9940) open CEntMaskDlg (dialog 6466, FUN_1801f8b80) and the colour / level mask dialogs
   (FUN_18064d9d0 / FUN_18065b670): NOT PORTED, greyed - the colour / level masks act as "same colour / level as the
   chained entity" (FUN_1801e3de0), the entity mask sets nothing. Kept without an effect here: Section angle (surface
   edges only), Break closest entity (lathe thread points), Break entities in Dynamic
   (FUN_1801e25e0 / 28e0 prompt 4230: NOT PORTED), Sync mode (no operation here syncs chains), Entity mask. Help greyed.
   The model: core_ui chain_opts_init / chain_opts_exclusive / chain_opts_ok (cui_chain.c); mcamui chain_opts_draw. */
ChainOptsDialog::ChainOptsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Chaining Options");
    dlg_font(this);
    chain_opts_init(&o_);                                /* FUN_1801f9a20 */
    setFixedSize(chainDu(this, 0, 0, 143, 403).size());
    auto R = [this](int x, int y, int w, int h) { return chainDu(this, x, y, w, h); };
    auto group = [&](int x, int y, int w, int h, const char *t) { auto *g = new QGroupBox(t, this); g->setGeometry(R(x, y, w, h)); };
    auto check = [&](int x, int y, int w, int h, const char *t, int *v) {
        auto *c = new QCheckBox(t, this); c->setGeometry(R(x, y, w, h)); c->setChecked(*v != 0); checks_.push_back({ c, v }); return c; };
    auto grey = [&](int x, int y, int w, int h, const char *t) { auto *p = new QPushButton(t, this); p->setGeometry(R(x, y, w, h)); p->setEnabled(false); };
    auto stat = [&](int x, int y, int w, int h, const char *t) { auto *l = new QLabel(t, this); l->setGeometry(R(x, y, w, h)); };
    auto real = [&](int x, int y, int w, int h, double v) { auto *e = new QLineEdit(QString::asprintf("%g", v), this); e->setGeometry(R(x, y, w, h)); return e; };
    group(7, 6, 130, 125, "Mask");
    check(12, 23, 10, 10, "", &o_.ent); grey(29, 17, 52, 18, "Entity");
    check(12, 45, 10, 10, "", &o_.col); grey(29, 40, 52, 18, "Color");
    check(12, 67, 10, 10, "", &o_.lev); grey(29, 63, 52, 18, "Level");
    check(12, 86, 87, 10, "Closed chains", &o_.closed); check(12, 99, 87, 10, "Open chains", &o_.open);
    QCheckBox *sect = check(12, 112, 77, 10, "Section angle", &o_.sect); ang_ = real(91, 110, 40, 14, o_.ang); ang_->setEnabled(o_.sect != 0);
    connect(sect, &QCheckBox::toggled, ang_, &QWidget::setEnabled);   /* FUN_1801f9870: 407 enabled by 547 */
    check(7, 135, 124, 10, "Ignore depths (in 3D mode)", &o_.ign);
    QCheckBox *spt = check(7, 147, 124, 10, "Start chain at point", &o_.spt);
    check(7, 159, 124, 10, "Allow surface edges in Single mode", &o_.surf); check(7, 171, 124, 10, "Break entities in Dynamic", &o_.dbrk);
    QCheckBox *brk = check(7, 183, 124, 10, "Break closest entity to thread point", &o_.brk);
    connect(spt, &QCheckBox::clicked, this, [spt, brk](bool on) { if (on) brk->setChecked(false); (void)spt; });   /* FUN_1801f98f0 (chain_opts_exclusive 0) */
    connect(brk, &QCheckBox::clicked, this, [spt, brk](bool on) { if (on) spt->setChecked(false); (void)brk; });   /* FUN_1801f9230 (chain_opts_exclusive 1) */
    group(7, 196, 130, 36, "Closed chains");
    cw_ = new QRadioButton("CW", this); cw_->setGeometry(R(13, 207, 60, 10)); ccw_ = new QRadioButton("CCW", this); ccw_->setGeometry(R(77, 206, 54, 10));
    { auto *g = new QButtonGroup(this); g->addButton(cw_); g->addButton(ccw_); (o_.ccw ? ccw_ : cw_)->setChecked(true); }
    check(13, 220, 118, 10, "Use cursor position", &o_.cur);
    group(7, 236, 130, 27, "Open chains");
    oneway_ = new QRadioButton("One way", this); oneway_->setGeometry(R(13, 248, 60, 10)); zig_ = new QRadioButton("Zigzag", this); zig_->setGeometry(R(77, 248, 54, 10));
    { auto *g = new QButtonGroup(this); g->addButton(oneway_); g->addButton(zig_); (o_.zig ? zig_ : oneway_)->setChecked(true); }
    group(7, 265, 130, 52, "Nested chains");
    stat(13, 278, 39, 10, "Sorting");
    sort_ = new QComboBox(this); sort_->setGeometry(R(55, 276, 75, 12)); for (auto *t : CHO_SORT) sort_->addItem(t); sort_->setCurrentIndex(o_.sort);
    check(13, 292, 118, 10, "Infinite nesting in area chains", &o_.inf); check(13, 303, 118, 10, "Reverse inner chains", &o_.rinv);
    stat(7, 324, 56, 11, "Sync mode");
    sync_ = new QComboBox(this); sync_->setGeometry(R(61, 322, 72, 12)); for (auto *t : CHO_SYNC) sync_->addItem(t); sync_->setCurrentIndex(o_.sync);
    stat(7, 343, 84, 8, "Chaining tolerance:"); tol_ = real(93, 340, 40, 14, o_.tol);
    stat(7, 362, 82, 8, "Plane tolerance:"); ptol_ = real(93, 358, 40, 14, o_.ptol);
    auto *ok = new QPushButton("OK", this); ok->setGeometry(R(18, 379, 36, 18)); ok->setDefault(true);
    auto *cancel = new QPushButton("Cancel", this); cancel->setGeometry(R(58, 379, 36, 18));
    grey(98, 379, 36, 18, "Help");
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    dlg_font(this);
}
/* MCReal edit: the value is taken when it parses and lies in [lo, hi] (mcamui REAL) */
static void real_take(QLineEdit *e, double *v, double lo, double hi) {
    QByteArray b = e->text().toUtf8(); char *end; double t = strtod(b.constData(), &end);
    if (end != b.constData() && t >= lo && t <= hi) *v = t;
}
void ChainOptsDialog::accept() {                         /* FUN_1801f9680 (core_ui chain_opts_ok) */
    for (auto &c : checks_) *c.second = c.first->isChecked();
    o_.ccw = ccw_->isChecked(); o_.zig = zig_->isChecked(); o_.sort = sort_->currentIndex(); o_.sync = sync_->currentIndex();
    if (o_.sect) real_take(ang_, &o_.ang, 0.0, 180.0);
    real_take(tol_, &o_.tol, cfg_sys_tol, 1e38); real_take(ptol_, &o_.ptol, cfg_sys_tol, 1e38);
    chain_opts_ok(&o_);
    QDialog::accept();
}
void ChainOptsDialog::reject() { show_chain_opts = 0; QDialog::reject(); }
void ChainOptsDialog::dump() const {
    printf("    chaining options:");
    for (auto &c : checks_) printf(" %s=%d", c.first->text().isEmpty() ? "mask" : c.first->text().toUtf8().constData(), c.first->isChecked());
    printf(" ccw=%d zig=%d sort=%d sync=%d tol=%s ptol=%s ang=%s\n", ccw_->isChecked(), zig_->isChecked(), sort_->currentIndex(), sync_->currentIndex(),
           tol_->text().toUtf8().constData(), ptol_->text().toUtf8().constData(), ang_->text().toUtf8().constData());
}
