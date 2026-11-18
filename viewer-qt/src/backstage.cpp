#include "backstage.h"
#include "icons.h"
#include "core_bridge.h"
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
extern "C" {
#include "scene.h"
}
#include "core_ui/core_ui.h"

/* File tab = BCGP backstage view, IDR_BCGP_RIBBON_XML CATEGORY_BACKSTAGE: its 15 entries in order (core_ui BS_ITEMS). Pages
   (forms, ribbonui BackstagePage*View / property sheets) show the ribbonuires dialog templates; commands run and close the
   backstage. icon = INDEX_SMALL in IDR_BCGP_IMAGE_CATEGORY_BACKSTAGE_SMALL (pages have none). (mcamui.c draw_backstage) */
/* colours / metrics: mcamui.c draw_backstage (ours - the BCGP visual manager's colours are NOT DECODED) */
static const QColor BS_BLUE(25, 82, 150), BS_SEL(43, 110, 185), BS_HOV(36, 98, 170);
static const double DX = 1.5, DY = 1.625;                 /* dialog units -> px (MS Shell Dlg 8 at 96 dpi: 6 x 13 base units) */
static QString caption(const char *t) { QString s = QString::fromUtf8(t); s.remove('&'); return s; }

/* the page area: x0 = 20 px right of the 200 px column, y0 = 16 (mcamui) */
static QRect ctlRect(const BsCtl &c, int x0, int y0) { return QRect((int)(x0 + c.x * DX), (int)(y0 + c.y * DY), (int)(c.w * DX), (int)(c.h * DY)); }
/* one page from its template (mcamui bs_page): statics word-wrapped (1057 = the page title, larger, blue), 1 px separators,
   buttons with their caption in the lower part (greyed unless live), edits / list boxes as frames */
static void drawPage(QPainter &p, const BsCtl *c, int n, int x0, int y0, int page, QPoint mouse) {
    QFont f = p.font(), big = f; big.setPixelSize(15);
    for (int i = 0; i < n; i++) {
        QRect r = ctlRect(c[i], x0, y0);
        if (c[i].cls == 0) {
            if (c[i].w <= 1) { p.setPen(QColor(200, 200, 200)); p.drawLine(r.x(), r.y(), r.x(), r.bottom()); continue; }   /* separator */
            p.setFont(c[i].id == 1057 ? big : f); p.setPen(c[i].id == 1057 ? BS_BLUE : QColor(30, 30, 30));
            p.drawText(r.adjusted(0, 0, 0, 40), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, caption(c[i].text));
            p.setFont(f);
        } else if (c[i].cls == 1) {
            bool on = bs_live(page, c[i].id), hov = on && r.contains(mouse);
            if (hov) p.fillRect(r, QColor(205, 225, 245));
            p.setPen(on ? QColor(150, 150, 150) : QColor(215, 215, 215)); p.drawRect(r.adjusted(0, 0, -1, -1));
            QFontMetrics fm(f); QRect tr = r;
            if (c[i].h >= 40) { tr.setTop(r.bottom() - 2 * (fm.height() + 2) - 2); tr.setHeight(2 * (fm.height() + 2)); }
            p.setPen(on ? QColor(20, 20, 20) : QColor(160, 160, 160));
            p.drawText(tr, Qt::AlignHCenter | (c[i].h >= 40 ? Qt::AlignTop : Qt::AlignVCenter) | Qt::TextWordWrap, caption(c[i].text));
        } else if (c[i].cls == 2 || c[i].cls == 3) { p.setPen(QColor(190, 190, 190)); p.drawRect(r.adjusted(0, 0, -1, -1)); }
    }
}
void BackstagePage::paintEvent(QPaintEvent *) {
    QPainter p(this); p.fillRect(rect(), Qt::white);
    QFont f = font(); f.setPixelSize(13); p.setFont(f); QFontMetrics fm(f);
    const int x0 = 20, y0 = 16;
    int pg = BS_ITEMS[item].page;
    if (pg == -1 || pg == -2) {                                /* Open / Save As property sheets: their pages at the left */
        static const char *open_pages[] = { "Recent Documents", "Computer" }, *save_pages[] = { "Computer" };
        const char **pn = pg == -1 ? open_pages : save_pages; int np = pg == -1 ? 2 : 1;
        if (sub >= np) sub = 0;
        QFont big = f; big.setPixelSize(15); p.setFont(big); p.setPen(BS_BLUE);
        p.drawText(QRect((int)(x0 + 24 * DX), y0, 400, 30), Qt::AlignLeft | Qt::AlignTop, BS_ITEMS[item].text); p.setFont(f);
        int py = (int)(y0 + 34 * DY);
        for (int k = 0; k < np; k++) {
            QRect b((int)(x0 + 24 * DX), py + k * 40, 220, 36);
            if (k == sub) p.fillRect(b, QColor(205, 225, 245)); else if (b.contains(mouse_)) p.fillRect(b, QColor(228, 238, 250));
            p.setPen(QColor(20, 20, 20)); p.drawText(b.adjusted(10, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, pn[k]);
        }
        int sx = (int)(x0 + 24 * DX + 240), sy = py;
        p.setPen(QColor(200, 200, 200)); p.drawLine(sx - 10, sy, sx - 10, sy + 400);
        if (pg == -1 && sub == 0) {                            /* dialog 129: Recent Documents list (1125) */
            drawPage(p, BS_129, bs_count(BS_129), sx, sy, 129, mouse_);
            int ly = (int)(sy + 23 * DY + 4);
            for (int k = 0; k < qc_recent_count(); k++) {
                QRect b((int)(sx + 7 * DX + 2), ly, (int)(301 * DX - 4), fm.height() + 6);
                if (b.contains(mouse_)) p.fillRect(b, QColor(205, 225, 245));
                p.setPen(QColor(20, 20, 20)); p.drawText(b.adjusted(4, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(qc_recent(k)));
                ly += fm.height() + 6;
            }
        } else drawPage(p, BS_130, bs_count(BS_130), sx, sy, 130, mouse_);   /* dialog 130: Computer - Browse (1127) */
        return;
    }
    if (pg <= 0) return;
    int n; const BsCtl *c = bs_page_ctls(pg, &n);
    drawPage(p, c, n, x0, y0, pg, mouse_);
    if (pg == 107) {                                           /* Info values (core_ui bs_info_values: the host callback FUN_1800fa5b0) */
        BsInfoVal V[8]; int nv = bs_info_values(V, 8);
        for (int v = 0; v < nv; v++) for (int i = 0; i < n; i++) if (c[i].id == V[v].id) {
            QRect r = ctlRect(c[i], x0, y0);
            if (V[v].id == 1173) { p.setPen(QColor(150, 150, 150)); p.drawText(QRect(r.x() + 4, r.y() + 3, r.width(), fm.height() + 2), Qt::AlignLeft | Qt::AlignTop, V[v].t); }   /* the cue banner (193) */
            else { p.setPen(QColor(30, 30, 30)); p.drawText(QRect(r.x(), r.y(), r.width(), fm.height() + 2), Qt::AlignLeft | Qt::AlignTop, QString::fromUtf8(V[v].t)); }
        }
    }
}
void BackstagePage::mouseMoveEvent(QMouseEvent *e) { mouse_ = e->position().toPoint(); update(); }
void BackstagePage::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) return;
    QPoint m = e->position().toPoint(); const int x0 = 20, y0 = 16;
    int pg = BS_ITEMS[item].page;
    if (pg == -1 || pg == -2) {
        int np = pg == -1 ? 2 : 1, py = (int)(y0 + 34 * DY);
        for (int k = 0; k < np; k++) if (QRect((int)(x0 + 24 * DX), py + k * 40, 220, 36).contains(m)) { sub = k; update(); return; }
        int sx = (int)(x0 + 24 * DX + 240), sy = py;
        if (pg == -1 && sub == 0) {
            QFont f = font(); f.setPixelSize(13); QFontMetrics fm(f); int ly = (int)(sy + 23 * DY + 4);
            for (int k = 0; k < qc_recent_count(); k++) { QRect b((int)(sx + 7 * DX + 2), ly, (int)(301 * DX - 4), fm.height() + 6);
                if (b.contains(m)) { emit recent(QString::fromUtf8(qc_recent(k))); return; } ly += fm.height() + 6; }
        } else for (const BsCtl *c = BS_130; c->text; c++) if (c->cls == 1 && bs_live(130, c->id) && ctlRect(*c, sx, sy).contains(m)) { emit hit(c->id); return; }
        return;
    }
    int n; const BsCtl *c = bs_page_ctls(pg, &n);
    for (int i = 0; i < n; i++) if (c[i].cls == 1 && bs_live(pg, c[i].id) && ctlRect(c[i], x0, y0).contains(m)) { emit hit(c[i].id); return; }
}

Backstage::Backstage(QWidget *parent) : QWidget(parent) {
    setAutoFillBackground(true);
    auto *h = new QHBoxLayout(this); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(0);
    left_ = new QWidget(this); left_->setFixedWidth(200); left_->setObjectName("bsLeft");
    left_->setStyleSheet(QString("#bsLeft { background: %1; } QPushButton { color: white; border: none; text-align: left; padding-left: 40px; height: 34px; background: transparent; }"
                                 " QPushButton:hover { background: %2; } QPushButton:checked { background: %3; } QPushButton:disabled { color: rgb(140,165,200); }"
                                 " QToolButton { border: 2px solid rgb(200,215,235); border-radius: 16px; color: white; background: transparent; }")
                         .arg(BS_BLUE.name(), BS_HOV.name(), BS_SEL.name()));
    auto *lv = new QVBoxLayout(left_); lv->setContentsMargins(0, 8, 0, 8); lv->setSpacing(0);
    auto *backb = new QToolButton(left_); backb->setFixedSize(32, 32); backb->setArrowType(Qt::LeftArrow); backb->setToolTip("Back");
    auto *bh = new QHBoxLayout; bh->setContentsMargins(12, 0, 0, 16); bh->addWidget(backb); bh->addStretch(1); lv->addLayout(bh);
    connect(backb, &QToolButton::clicked, this, &Backstage::back);
    page_ = new BackstagePage(this);
    for (int i = 0; i < BS_NITEMS; i++) {
        auto *b = new QPushButton(BS_ITEMS[i].text, left_); b->setFlat(true); b->setFixedHeight(34);
        if (BS_ITEMS[i].icon >= 0) { b->setIcon(Icons::icon(QString("#file:IDR_BCGP_IMAGE_CATEGORY_BACKSTAGE_SMALL_%1.png").arg(BS_ITEMS[i].icon))); b->setStyleSheet("padding-left: 16px;"); }
        b->setEnabled(itemEnabled(i));                  /* mcamui: live = a page or a command (New, Save) */
        b->setCheckable(BS_ITEMS[i].page != 0);
        lv->addWidget(b); items_ << b;
        connect(b, &QPushButton::clicked, this, [this, i] {
            if (BS_ITEMS[i].cmd == BS_CMD_NEW) emit newPart();
            else if (BS_ITEMS[i].cmd == BS_CMD_SAVE) emit save();
            else if (BS_ITEMS[i].cmd == BS_CMD_CONFIG) emit configuration();
            else showPage(i);
        });
    }
    lv->addStretch(1);
    h->addWidget(left_); h->addWidget(page_, 1);
    connect(page_, &BackstagePage::recent, this, &Backstage::openPath);
    connect(page_, &BackstagePage::hit, this, [this](int id) {
        if (id == 1099) emit about();                   /* Help > About apprenticeCAM = OnAppAbout (mcamui called run_local("About"): nothing - phase 8) */
        else if (id == 1127) { if (BS_ITEMS[page_->item].page == -1) emit openDialog(); else emit saveAs(); }   /* Computer > Browse */
    });
    showPage(0);                                        /* mcamui bs_sel = 0: Info */
}
bool Backstage::itemEnabled(int i) const { return i >= 0 && i < BS_NITEMS && (BS_ITEMS[i].page != 0 || BS_ITEMS[i].cmd); }
bool Backstage::pageButtonEnabled(int page, unsigned id) const { return bs_live(page, id) != 0; }
void Backstage::showPage(int i) {
    if (i < 0 || i >= BS_NITEMS || !BS_ITEMS[i].page) return;
    page_->item = i; page_->sub = 0;
    for (int k = 0; k < items_.size(); k++) items_[k]->setChecked(k == i);
    refresh();
}
void Backstage::refresh() { page_->update(); }
void Backstage::paintEvent(QPaintEvent *) { QPainter p(this); p.fillRect(rect(), Qt::white); }
