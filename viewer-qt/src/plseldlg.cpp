#include "plseldlg.h"
#include "icons.h"
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QDoubleValidator>
#include <QImage>
#include <QToolTip>
#include <cstdio>

/* a 1085 bitmap button's picture: the icon pack's <dll>_BMPK_<id>.png (192,192,192 keyed by extract_icons.py), else <dll>_BMP_<id>.png
   with 192,192,192 made clear here (UIBitmapButton::SetTransparent + SetTransparencyColor(0xc0c0c0), CPlaneDlg FUN_180238ff0). bmp > 0:
   mccoreres (the core resource handle); bmp < 0: opmanres (GetMastercamResourceHandle) */
static QPixmap psBitmap(int bmp) {
    if (bmp == 0) return QPixmap();
    const char *dll = bmp > 0 ? "mccoreres" : "opmanres"; int id = bmp > 0 ? bmp : -bmp;
    QPixmap k = Icons::image(QString("#file:%1_BMPK_%2.png").arg(dll).arg(id));
    if (!k.isNull()) return k;
    QPixmap p = Icons::image(QString("#file:%1_BMP_%2.png").arg(dll).arg(id));
    if (p.isNull()) return p;
    QImage im = p.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < im.height(); y++) { QRgb *row = (QRgb *)im.scanLine(y); for (int x = 0; x < im.width(); x++) if ((row[x] & 0xffffff) == 0xc0c0c0) row[x] = 0; }
    return QPixmap::fromImage(im);
}

PlaneSelDialog::PlaneSelDialog(QWidget *parent) : FloatDlg("Plane Selection", parent) {
    setObjectName("planeSel1085"); setClosable(false);   /* the template's caption; the close box = Cancel (NOT DECODED: none drawn) */
    setBodySize(chainDu(this, 0, 0, PSEL1085_W, PSEL1085_H).size());
    QWidget *b = body();
    for (int i = 0; i < PSEL1085_N; i++) {
        const PselCtl &c = PSEL1085[i]; QRect r = chainDu(this, c.x, c.y, c.w, c.h); QString t = QString::fromUtf8(c.text);
        switch (c.kind) {
        case PSK_GROUP: { auto *g = new QGroupBox(t, b); g->setGeometry(r); g->lower(); g->show(); } break;
        case PSK_EDIT: {
            int ax = c.id - 964; auto *e = edit_[ax] = new QLineEdit(b); e->setGeometry(r); e->setObjectName(QString("pselEdit%1").arg(c.id));
            auto *v = new QDoubleValidator(e); v->setNotation(QDoubleValidator::StandardNotation); e->setValidator(v); e->show();
            connect(e, &QLineEdit::textEdited, this, [this, ax](const QString &s) { if (filling_) return; QByteArray u = s.toUtf8(); run([ax, u] { psel_set_text(ax, u.constData()); }); });
            connect(e, &QLineEdit::editingFinished, this, [this, ax] { if (filling_ || !psel_open) return; run([ax] { psel_edit_done(ax); }); });   /* EN_KILLFOCUS / <Enter> */
            /* the buddy (UIEdit::EnableBuddyCtl uictrls 0x18000f210: an up-down control, style 0x50000024 = WS_CHILD | WS_VISIBLE | UDS_ALIGNRIGHT
               | UDS_ARROWKEYS, range -100 .. 100, buddy = the edit): its arrows at the edit's right; up = +1 */
            int bw = 12; e->setGeometry(r.adjusted(0, 0, -bw, 0));
            for (int dir = 0; dir < 2; dir++) {
                auto *sb = new QToolButton(b); sb->setArrowType(dir ? Qt::DownArrow : Qt::UpArrow); sb->setObjectName(QString("pselSpin%1%2").arg(c.id).arg(dir ? "dn" : "up"));
                sb->setGeometry(r.right() - bw + 1, r.top() + (dir ? r.height() / 2 : 0), bw, r.height() / 2 + (dir ? r.height() % 2 : 0)); sb->setAutoRepeat(true); sb->show();
                spin_[ax][dir] = sb; int d = dir ? -1 : 1;
                connect(sb, &QToolButton::clicked, this, [this, ax, d] { if (filling_ || !psel_open) return; run([ax, d] { psel_spin(ax, d); }); });   /* UDN_DELTAPOS iDelta */
            }
        } break;
        case PSK_BMP: case PSK_BTN: {
            QAbstractButton *w;
            if (c.kind == PSK_BTN) { auto *p = new QPushButton(t, b); w = p; }
            else { auto *tb = new QToolButton(b); QPixmap pm = psBitmap(c.bmp); if (!pm.isNull()) { tb->setIcon(QIcon(pm)); tb->setIconSize(pm.size()); } w = tb; }
            w->setGeometry(r); w->setToolTip(t); w->setObjectName(QString("pselBtn%1").arg(c.id)); w->setEnabled(c.ref == 0); w->show();
            btn_.insert(c.id, w);
            int id = c.id;
            connect(w, &QAbstractButton::clicked, this, [this, id] { if (filling_) return; run([id] { psel_ctl(id); }); });
        } break;
        }
    }
}
void PlaneSelDialog::run(const std::function<void()> &fn) { if (withGL) withGL(fn); else fn(); emit changed(); }
/* the model's state: the edits' texts after an UpdateData(FALSE) (typed text is kept until then), the Flip bitmap (1124 when set) */
void PlaneSelDialog::refresh() {
    filling_ = true;
    if (upd_ != psel_upd) { upd_ = psel_upd; for (int i = 0; i < 3; i++) if (edit_[i]) edit_[i]->setText(QString::fromUtf8(psel_text[i])); }
    if (psel_errtip[0] && psel_errtip_axis >= 0 && psel_errtip_axis < 3 && edit_[psel_errtip_axis] && isVisible())   /* UIErrorTip::Show at the edit */
        QToolTip::showText(edit_[psel_errtip_axis]->mapToGlobal(QPoint(0, edit_[psel_errtip_axis]->height())), QString::fromUtf8(psel_errtip), edit_[psel_errtip_axis]);
    if (flipShown_ != psel_flip) {
        flipShown_ = psel_flip;
        if (auto *tb = qobject_cast<QToolButton *>(btn_.value(575))) { QPixmap pm = psBitmap(psel_flip ? 1124 : 1123); if (!pm.isNull()) { tb->setIcon(QIcon(pm)); tb->setIconSize(pm.size()); } }
    }
    filling_ = false;
}
void PlaneSelDialog::dump() const {
    printf("    pseldlg edits \"%s\" \"%s\" \"%s\" flip %d buttons", edit_[0]->text().toUtf8().constData(), edit_[1]->text().toUtf8().constData(),
           edit_[2]->text().toUtf8().constData(), flipShown_);
    for (int i = 0; i < PSEL1085_N; i++) if (PSEL1085[i].kind == PSK_BMP || PSEL1085[i].kind == PSK_BTN) {
        QAbstractButton *w = btn_.value(PSEL1085[i].id);
        printf(" %d%s%s", PSEL1085[i].id, w && w->isEnabled() ? "" : "(grey)", w && !w->icon().isNull() ? "" : (PSEL1085[i].kind == PSK_BMP ? "(nobmp)" : ""));
    }
    printf("\n");
}
bool PlaneSelDialog::testSet(const QString &key, const QString &v) {
    if (key == "btn") { QAbstractButton *w = btn_.value(v.toInt()); if (!w || !w->isEnabled() || !isVisible()) return false; w->click(); return true; }
    if (key == "spin") {                                  /* spin=I:up|dn - an arrow of edit I's buddy */
        int i = v.section(':', 0, 0).toInt(); if (i < 0 || i > 2 || !isVisible()) return false;
        QToolButton *sb = spin_[i][v.section(':', 1) == "dn" ? 1 : 0]; if (!sb) return false; sb->click(); return true;
    }
    if (key == "edit") {                                  /* edit=I:TEXT - typed into the edit (textEdited), then the focus leaves it */
        int i = v.section(':', 0, 0).toInt(); if (i < 0 || i > 2 || !edit_[i] || !isVisible()) return false;
        edit_[i]->setText(v.section(':', 1)); emit edit_[i]->textEdited(edit_[i]->text()); emit edit_[i]->editingFinished(); return true;
    }
    return false;
}
