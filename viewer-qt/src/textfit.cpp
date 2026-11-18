#include "textfit.h"
#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QAbstractButton>
#include <QPushButton>
#include <QCheckBox>
#include <QRadioButton>
#include <QToolButton>
#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QTabBar>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QHeaderView>
#include <QStyle>
#include <QProxyStyle>
#include <QImage>
#include <QPixmap>
#include "icons.h"
#include <QPainter>
#include <QToolTip>
#include <QTableView>
#include <QHelpEvent>
#include <QPalette>
#include "contrast.h"
#include <QFontDatabase>
#include <QCoreApplication>
#define DIALOG_FONT_FILE "tahoma.ttf"
#include <QStyleOption>
#include <QFontMetrics>
#include <QTextDocument>
#include <QSet>
#include <QFile>
#include <QMenu>
#include <QScreen>
#include <QEvent>
#include <QPointer>
#include <QTimer>
#include <QLayout>
#include <QTreeWidget>
#include <QTreeView>
#include <QMainWindow>
#include <cstdio>
#include <cstdlib>

/* ------------------------------------------------------------------------------------------------ the shared fixes */
namespace {
/* the width a combo's popup list needs for its longest item (the view's delegate: text + icon + the style's item margins)
   plus the list frame and a vertical scroll bar when the list scrolls */
/* the width item I needs in the popup list: the menu-style delegate (QComboMenuDelegate, styles whose SH_ComboBox_Popup is
   on, e.g. Fusion) sizes items as menu items (CT_MenuItem: text + check column + margins, at least ~120 px whatever the
   text): its overhead measured on a long text and added to the item's own text width; the list delegate (QItemDelegate)
   sizes an item to its text exactly */
int itemNeed(QComboBox *cb, int i) {
    QAbstractItemView *v = cb->view();
    QModelIndex ix = cb->model()->index(i, cb->modelColumn(), cb->rootModelIndex());
    if (!v->itemDelegateForIndex(ix)->inherits("QComboMenuDelegate")) return v->sizeHintForIndex(ix).width();
    QStyleOptionMenuItem o; o.initFrom(v);
    o.menuItemType = QStyleOptionMenuItem::Normal; o.checkType = QStyleOptionMenuItem::NonExclusive; o.menuHasCheckableItems = true;
    o.font = v->font(); o.maxIconWidth = 0; o.reservedShortcutWidth = 0;
    QFontMetrics fm(o.font);
    o.text = QString(60, QChar('W')); int adv = fm.horizontalAdvance(o.text);
    int over = cb->style()->sizeFromContents(QStyle::CT_MenuItem, &o, QSize(adv, fm.height()), v).width() - adv;
    int iw = cb->itemIcon(i).isNull() ? 0 : cb->iconSize().width() + 4;
    return fm.horizontalAdvance(cb->itemText(i)) + iw + over;
}
int popupNeed(QComboBox *cb) {
    QAbstractItemView *v = cb->view();
    if (!v || !cb->model()) return 0;
    int w = 0;
    for (int r = 0; r < cb->count(); r++) w = std::max(w, itemNeed(cb, r));
    if (w <= 0) return 0;
    QWidget *c = v->parentWidget();                       /* QComboBoxPrivateContainer: its frame, margins, scroll bar */
    int frame = c ? c->width() - v->viewport()->width() : 0;
    if (frame < 0) frame = 0;
    return w + frame;
}

/* the multi-line text's widest line */
int textAdv(const QFontMetrics &fm, const QString &t) {
    int w = 0; for (const QString &l : t.split('\n')) w = std::max(w, fm.horizontalAdvance(l)); return w;
}
QString shownText(QString t) {                            /* a button's mnemonic text as drawn: "&&" = '&', "&x" = 'x' */
    QString o; for (int i = 0; i < t.size(); i++) { if (t[i] == '&' && i + 1 < t.size()) i++; o += t[i]; } return o;
}
bool isRich(const QLabel *l) { return l->textFormat() == Qt::RichText || (l->textFormat() == Qt::AutoText && Qt::mightBeRichText(l->text())); }

/* the width W needs for its whole text on one line (0 = not a text widget this fit handles) */
int needWidth(QWidget *w) {
    QFontMetrics fm = w->fontMetrics(); QStyle *st = w->style();
    if (auto *l = qobject_cast<QLabel *>(w)) {
        if (!l->pixmap().isNull() || l->text().trimmed().isEmpty() || isRich(l)) return 0;
        int ind = l->indent() < 0 ? (l->frameWidth() > 0 ? fm.horizontalAdvance('x') / 2 : 0) : l->indent();
        return textAdv(fm, l->buddy() ? shownText(l->text()) : l->text()) + (l->width() - l->contentsRect().width()) + 2 * l->margin() + std::max(ind, 0) + 1;
    }
    if (qobject_cast<QCheckBox *>(w) || qobject_cast<QRadioButton *>(w)) {
        auto *b = static_cast<QAbstractButton *>(w); if (b->text().isEmpty()) return 0;
        QStyleOptionButton o; o.initFrom(b); o.text = b->text(); o.icon = b->icon(); o.iconSize = b->iconSize();
        QRect cr = st->subElementRect(qobject_cast<QCheckBox *>(w) ? QStyle::SE_CheckBoxContents : QStyle::SE_RadioButtonContents, &o, b);
        int iw = b->icon().isNull() ? 0 : b->iconSize().width() + 4;
        if (b->property("textfit_multiline").toBool()) {
            QRect br = fm.boundingRect(QRect(0, 0, cr.width(), 100000), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, shownText(b->text()));
            if (br.width() <= cr.width() + 1 && br.height() <= cr.height() + 2) return 0;
        }
        return textAdv(fm, shownText(b->text())) + iw + (b->width() - cr.width()) + 1;
    }
    if (auto *cb = qobject_cast<QComboBox *>(w)) {
        QStyleOptionComboBox o; o.initFrom(cb); o.editable = cb->isEditable(); o.frame = cb->hasFrame();
        QRect field = st->subControlRect(QStyle::CC_ComboBox, &o, QStyle::SC_ComboBoxEditField, cb);
        int t = 0;
        for (int i = 0; i < cb->count(); i++) t = std::max(t, fm.horizontalAdvance(cb->itemText(i)) + (cb->itemIcon(i).isNull() ? 0 : cb->iconSize().width() + 4));
        return t ? t + (cb->width() - field.width()) + 1 : 0;
    }
    return 0;
}

/* Mastercam's dialog templates (and our own fixed-position dialogs) size a text control for its text in MS Shell Dlg 8; our
   UI font is wider, so the text of a label / check box / radio / closed combo can run past the control. A control placed
   by hand (no layout) whose text does not fit grows - never moving its anchored edge - into the free space beside it (right
   for left-aligned text, left for right-aligned, both ways for centred), up to the nearest sibling that shares its rows,
   its group box's frame or the parent's edge; every sibling counts (shown or not: a hidden one can appear). A static whose
   rectangle has room for two lines or more wraps, as a Windows static (SS_LEFT) does, and grows only as far as its
   wrapped lines need. What still does not fit is left as it is (the checker reports it). */
void fitChildren(QWidget *p) {
    const QList<QWidget *> kids = p->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : kids) {
        if (w->isWindow() || (p->layout() && p->layout()->indexOf(w) >= 0)) continue;
        int need = needWidth(w);
        if (need <= w->width()) continue;
        auto *lab = qobject_cast<QLabel *>(w);
        Qt::Alignment al = lab ? lab->alignment() : Qt::AlignLeft;
        int mode = (al & Qt::AlignHCenter) ? 2 : (al & Qt::AlignRight) ? 1 : 0;   /* grows: 0 right, 1 left, 2 both */
        QRect g = w->geometry();
        int limR = p->width() - 1, limL = 0;               /* the free space on its rows */
        for (QWidget *s : kids) {
            if (s == w || s->isWindow() || s->width() <= 0 || s->height() <= 0) continue;
            QRect r = s->geometry();
            if (r.top() > g.bottom() || r.bottom() < g.top()) continue;   /* not on its rows */
            bool frame = qobject_cast<QGroupBox *>(s) || (qobject_cast<QFrame *>(s) && !qobject_cast<QLabel *>(s) && static_cast<QFrame *>(s)->frameShape() == QFrame::Box);
            if (frame && r.contains(g.center())) { limR = std::min(limR, r.right() - 6); limL = std::max(limL, r.left() + 6); continue; }   /* inside its box: the frame */
            if (r.left() >= g.right() - 1) limR = std::min(limR, r.left() - 3);
            if (r.right() <= g.left() + 1) limL = std::max(limL, r.right() + 3);
        }
        int extraMax = mode == 0 ? limR - g.right() : mode == 1 ? g.left() - limL : 2 * std::min(limR - g.right(), g.left() - limL);
        if (extraMax < 0) extraMax = 0;
        if (lab && !isRich(lab)) {
            QFontMetrics fm = lab->fontMetrics(); QRect cr = lab->contentsRect();
            int fl = int(al) | Qt::TextWordWrap;
            auto wrapped = [&](int wd) { return fm.boundingRect(QRect(0, 0, wd, 100000), fl, lab->text()); };
            if (lab->wordWrap() || cr.height() >= 2 * fm.lineSpacing() - 2) {   /* wrapped: the narrowest width whose lines fit */
                int add = 0;
                for (; add <= extraMax; add += 2) { QRect br = wrapped(cr.width() + add); if (br.width() <= cr.width() + add + 1 && br.height() <= cr.height() + 2) break; }
                if (add > extraMax) add = extraMax;
                lab->setWordWrap(true);
                need = w->width() + add;
            }
        }
        int extra = std::min(need - w->width(), extraMax);
        if (extra <= 0) continue;
        if (mode == 0) w->resize(g.width() + extra, g.height());
        else if (mode == 1) w->setGeometry(g.left() - extra, g.top(), g.width() + extra, g.height());
        else w->setGeometry(g.left() - extra / 2, g.top(), g.width() + extra / 2 * 2, g.height());
    }
}

/* Windows combo boxes drop their list at the combo's width; Mastercam's longest items fit their lists at its 8 pt dialog font.
   The Qt list (QComboBox::showPopup) is the combo's width too and elides an item that is wider in our font: when the
   popup container is shown it is widened to its longest item (kept on the combo's screen, left edge at the combo).
   A label / check box / radio / combo shown in a hand-placed parent queues the parent for fitChildren (after the page is
   built: the event loop's next turn). */
class FitFilter : public QObject {
public:
    using QObject::QObject;
    QList<QPointer<QWidget>> pending_; bool armed_ = false;
    void queue(QWidget *p) {
        for (const auto &q : pending_) if (q == p) return;
        pending_.append(p);
        if (!armed_) { armed_ = true; QTimer::singleShot(0, this, [this] { armed_ = false; auto l = pending_; pending_.clear(); for (auto &q : l) if (q) fitChildren(q); }); }
    }
    bool eventFilter(QObject *o, QEvent *e) override {
        /* user decision UI-5: an edit / closed combo whose value is wider than its field shows the whole value as its tooltip
           (the field scrolls as usual); a tooltip of its own comes first */
        if (e->type() == QEvent::ToolTip && o->isWidgetType()) {
            QWidget *w = static_cast<QWidget *>(o); QString v; int room = 0;
            if (auto *le = qobject_cast<QLineEdit *>(w)) { v = le->text(); room = le->width() - 8; }
            else if (auto *cb = qobject_cast<QComboBox *>(w)) {
                QStyleOptionComboBox opt; opt.initFrom(cb); opt.editable = cb->isEditable();
                v = cb->currentText(); room = cb->style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, cb).width();
            }
            if (!v.isEmpty() && w->fontMetrics().horizontalAdvance(v) > room && !w->toolTip().contains(v)) {
                auto *he = static_cast<QHelpEvent *>(e);
                QToolTip::showText(he->globalPos(), w->toolTip().isEmpty() ? v : w->toolTip() + "\n" + v, w);
                return true;
            }
        }
        if (e->type() == QEvent::Show && o->isWidgetType()) {
            QWidget *c = static_cast<QWidget *>(o);
            if (!c->isWindow() && (qobject_cast<QLabel *>(c) || qobject_cast<QCheckBox *>(c) || qobject_cast<QRadioButton *>(c) || qobject_cast<QComboBox *>(c))) {
                QWidget *p = c->parentWidget();
                if (p && !qobject_cast<QMainWindow *>(c->window()) && !(p->layout() && p->layout()->indexOf(c) >= 0)
                    && !qobject_cast<QAbstractScrollArea *>(p->parentWidget())) queue(p);
            }
            /* a tree without a header (the managers, the op dialog's page tree): Windows tree controls scroll sideways to a
               long item instead of cutting it; the one column follows its items' width (a horizontal scroll bar when wider) */
            if (auto *tv = qobject_cast<QTreeView *>(c); tv && tv->isHeaderHidden() && tv->model() && tv->model()->columnCount() == 1
                && tv->header()->sectionResizeMode(0) != QHeaderView::ResizeToContents) {
                tv->header()->setStretchLastSection(false);
                tv->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
            }
            if (c->isWindow() && c->inherits("QComboBoxPrivateContainer")) {
                QComboBox *cb = qobject_cast<QComboBox *>(c->parentWidget());
                if (cb) {
                    int need = popupNeed(cb);
                    if (need > c->width()) {
                        QRect g = c->geometry(); g.setWidth(need);
                        if (QScreen *s = cb->screen()) { QRect a = s->availableGeometry(); if (g.right() > a.right()) g.moveRight(a.right()); if (g.left() < a.left()) g.moveLeft(a.left()); }
                        c->setGeometry(g);
                    }
                }
            }
        }
        return false;
    }
};
}

namespace TextFit {
bool on() { static int v = qEnvironmentVariableIsSet("MCAMUI_TEXTFIT_OFF") ? 0 : 1; return v; }   /* tests: MCAMUI_TEXTFIT_OFF=1 = the fixes off (before / after counts) */
/* the gap between a check box / radio button's indicator and its text: a Win32 button draws its text about 3 px after the
   13 px box (Qt's styles leave 4 - 6 px), which Mastercam's templates are sized for */
class FitStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    int pixelMetric(PixelMetric m, const QStyleOption *o, const QWidget *w) const override {
        int v = QProxyStyle::pixelMetric(m, o, w);
        if (m == PM_CheckBoxLabelSpacing || m == PM_RadioButtonLabelSpacing) return std::min(v, 3);
        return v;
    }
    /* a multi-line check box / radio (property textfit_multiline: a template control of two lines' height, BS_MULTILINE |
       BS_TOP as mccoreres 6555 2252): the text wraps in its rectangle from the top, the indicator at the first line */
    static bool multi(const QWidget *w) { return w && w->property("textfit_multiline").toBool(); }
    void drawControl(ControlElement ce, const QStyleOption *o, QPainter *p, const QWidget *w) const override {
        if ((ce == CE_CheckBoxLabel || ce == CE_RadioButtonLabel) && multi(w))
            if (auto *b = qstyleoption_cast<const QStyleOptionButton *>(o)) {
                proxy()->drawItemText(p, b->rect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextShowMnemonic, b->palette,
                                      (b->state & State_Enabled) != 0, b->text, QPalette::WindowText);
                return;
            }
        QProxyStyle::drawControl(ce, o, p, w);
    }
    QRect subElementRect(SubElement se, const QStyleOption *o, const QWidget *w) const override {
        QRect r = QProxyStyle::subElementRect(se, o, w);
        if ((se == SE_CheckBoxIndicator || se == SE_RadioButtonIndicator) && multi(w) && w)
            r.moveTop(o->rect.top() + std::max(0, (w->fontMetrics().height() - r.height()) / 2));
        return r;
    }
};
/* the dialog templates' font (user decision UI-4, 2026-11-17): Mastercam's templates ask for MS Sans Serif 8 (1342 of the
   resource DLLs' dialogs) or MS Shell Dlg 8 = Tahoma 8 (DS_SHELLFONT, 230); both give the 6 x 13 base units the layouts
   (chainDu) are in. DIALOG_FONT is a freely licensed font of those advance widths, bundled in viewer-qt/fonts (see its
   README); MCAMUI_DLGFONT=FAMILY:PX overrides it (tests: the width comparison) */
static QString g_dlg_family; static int g_dlg_px = 11;
static void loadFonts() {
    const QString dirs[] = { QCoreApplication::applicationDirPath() + "/fonts", QStringLiteral("fonts") };
    for (const QString &d : dirs) {
        QFile f(d + "/" DIALOG_FONT_FILE);
        if (!f.exists()) continue;
        int id = QFontDatabase::addApplicationFont(f.fileName());
        if (id >= 0 && !QFontDatabase::applicationFontFamilies(id).isEmpty()) { g_dlg_family = QFontDatabase::applicationFontFamilies(id).first(); break; }
    }
    QByteArray o = qgetenv("MCAMUI_DLGFONT");
    if (!o.isEmpty()) { QList<QByteArray> p = o.split(':'); g_dlg_family = QString::fromUtf8(p[0]); if (p.size() > 1) g_dlg_px = p[1].toInt(); }
}
/* user decisions UI-2 (tool lists) / UI-5 (Quick View): a column never narrower than its header and its cells; the width
   it had (Mastercam's default / the user's) is kept when wider. Not persisted: the list's saved widths stay as they are */
void fitColumns(QTableView *t) {
    if (!t || !t->model()) return;
    for (int c = 0; c < t->model()->columnCount(); c++) {
        if (t->isColumnHidden(c)) continue;
        int w0 = t->columnWidth(c); t->resizeColumnToContents(c);
        if (t->columnWidth(c) < w0) t->setColumnWidth(c, w0);
    }
}
/* user decision UI-3 (2026-11-17): a control this program fills differently ("(ours)") or whose meaning is NOT DECODED keeps
   Mastercam's text; the marker is its text colour (one colour app-wide: the "(ours)" brown rgb(150, 90, 0), darkened / lightened
   to WCAG 4.5:1 on the theme's background, 3:1 when greyed) and the note is its tooltip */
const QColor MARKER_COLOR(150, 90, 0);
void markOurs(QWidget *w, const QString &tip) {
    if (!w) return;
    QPalette pal = w->palette();
    for (auto grp : { QPalette::Active, QPalette::Inactive, QPalette::Disabled }) {
        double ratio = grp == QPalette::Disabled ? 3.0 : 4.5;
        for (auto [fg, bg] : { std::pair{ QPalette::WindowText, QPalette::Window }, std::pair{ QPalette::ButtonText, QPalette::Button },
                               std::pair{ QPalette::Text, QPalette::Base } })
            pal.setColor(grp, fg, readableOn(grp == QPalette::Disabled ? QColor(190, 140, 70) : MARKER_COLOR, pal.color(grp, bg), ratio));
    }
    w->setPalette(pal);
    w->setProperty("textfit_marked", true);
    QString t = w->toolTip();
    if (!t.contains(tip)) w->setToolTip(t.isEmpty() ? tip : t + "\n" + tip);
}
/* Mastercam's bitmap buttons (UI-5, the Mastercam-default rule): the bitmap from the icon pack with its transparency colour
   cleared, as UIBitmapButton::SetTransparencyColor (0xC0C0C0) / CToolFilt::InitBitmaps (0xFF00FF) draw it */
QPixmap keyedBitmap(const QString &packName, unsigned key) {
    QString f = "#file:" + packName;
    if (!Icons::hasImage(f)) return QPixmap();
    QImage im = Icons::image(f).toImage().convertToFormat(QImage::Format_ARGB32);
    const QRgb k = 0xff000000u | key;
    for (int y = 0; y < im.height(); y++) { QRgb *row = (QRgb *)im.scanLine(y); for (int x = 0; x < im.width(); x++) if ((row[x] | 0xff000000u) == k) row[x] = 0; }
    return QPixmap::fromImage(im);
}
QFont dialogFont(QFont base) {
    if (!g_dlg_family.isEmpty()) base.setFamily(g_dlg_family);
    base.setPixelSize(g_dlg_px);
    return base;
}
QString dialogFontCss() {
    QString c = QString("font-size: %1px;").arg(g_dlg_px);
    if (!g_dlg_family.isEmpty()) c += QString(" font-family: \"%1\";").arg(g_dlg_family);
    return c;
}
void install(QApplication *app) {
    loadFonts();
    if (!on()) return;
    app->setStyle(new FitStyle(QApplication::style()->name()));   /* the desktop's style (or QT_STYLE_OVERRIDE / -style), proxied */
    app->installEventFilter(new FitFilter(app));
}
bool enabled() { return qEnvironmentVariableIsSet("MCAMUI_TEXTCHECK"); }

/* ------------------------------------------------------------------------------------------------ the checker */
namespace {
struct TB : QTabBar { using QTabBar::tabSizeHint; };
struct HV : QHeaderView { using QHeaderView::sectionSizeFromContents; };
struct AV : QAbstractItemView { using QAbstractItemView::initViewItemOption; };

QSet<QString> g_seen;
FILE *g_out = nullptr;
const char *g_where = "";
int g_count = 0;

QString plain(QString t) {                                /* the shown text of a mnemonic text: "&&" = '&', "&x" = 'x' */
    QString o; for (int i = 0; i < t.size(); i++) { if (t[i] == '&' && i + 1 < t.size()) i++; o += t[i]; } return o;
}
QString winName(QWidget *tl) {
    QString n = tl->objectName();
    QString t = tl->windowTitle();
    if (qobject_cast<QMainWindow *>(tl)) return "MainWindow";   /* its title holds the document name */
    if (n.isEmpty()) n = tl->metaObject()->className();
    if (!t.isEmpty() && t != n) n += " \"" + t + "\"";
    return n;
}
QString pathOf(QWidget *w, QWidget *tl) {
    QStringList p;
    for (QWidget *q = w; q && q != tl; q = q->parentWidget()) p.prepend(q->objectName().isEmpty() ? q->metaObject()->className() : q->objectName());
    while (p.size() > 4) p.removeFirst();
    return p.join('/');
}
QString esc(QString t) { t.replace('\n', "\\n"); t.replace('\t', ' '); return t; }

/* the part of W's rectangle its ancestors show (W's coordinates); a scroll area's viewport ends the clipping (scrollable) */
QRect visibleRect(QWidget *w, QWidget *tl) {
    QRect r = w->rect(); QPoint off(0, 0); QWidget *c = w;
    while (c != tl && c->parentWidget()) {
        QWidget *p = c->parentWidget();
        if (qobject_cast<QAbstractScrollArea *>(p->parentWidget()) && static_cast<QAbstractScrollArea *>(p->parentWidget())->viewport() == p) break;
        if (QByteArray(p->metaObject()->className()).startsWith("SARibbonCategory")) break;   /* the ribbon scrolls a tab's panels sideways (a narrow window) */
        off += c->pos();
        r &= p->rect().translated(-off);
        c = p;
    }
    return r;
}

void report(const char *kind, QWidget *tl, QWidget *w, const QString &text, const QRect &ink, const QRect &have, const QString &extra = QString()) {
    QString key = QString("%1|%2|%3|%4|%5x%6").arg(kind, winName(tl), w->metaObject()->className(), text).arg(have.width()).arg(have.height());
    if (g_seen.contains(key)) return;
    g_seen.insert(key); g_count++;
    QString line = QString("%1\t%2\t%3\t\"%4\"\tneed %5x%6\thave %7x%8\t%9%10\n").arg(kind, winName(tl), pathOf(w, tl), esc(text))
                       .arg(ink.width()).arg(ink.height()).arg(have.width()).arg(have.height()).arg(QString::fromUtf8(g_where), extra.isEmpty() ? QString() : "\t" + extra);
    fputs(line.toUtf8().constData(), g_out);
}

/* the glyphs' vertical extent inside a text block laid out from fm's line boxes: the block less the top / bottom spare */
QRect inkV(const QFontMetrics &fm, QRect br) {
    QRect t = fm.tightBoundingRect("Agy");
    int top = fm.ascent() + t.top(), bot = fm.height() - (fm.ascent() + t.bottom() + 1);
    if (top > 0) br.setTop(br.top() + top);
    if (bot > 0) br.setBottom(br.bottom() - bot);
    return br;
}
const int TOL = 1;
bool exceeds(const QRect &ink, const QRect &have) {
    return ink.left() < have.left() - TOL || ink.right() > have.right() + TOL || ink.top() < have.top() - TOL || ink.bottom() > have.bottom() + TOL;
}

struct Ink { QWidget *w; QRect r; QString text; bool pic; };

/* the text rectangle a single-line text takes in AREA with alignment AL */
QRect textRect(const QFontMetrics &fm, const QRect &area, int al, const QString &t) {
    return inkV(fm, fm.boundingRect(area, al | Qt::TextShowMnemonic, t));
}

void checkWidget(QWidget *w, QWidget *tl, QVector<Ink> &inks) {
    QFontMetrics fm = w->fontMetrics(); QStyle *st = w->style();
    QRect vis = visibleRect(w, tl);
    auto inkOf = [&](const QRect &r, const QString &t, bool pic = false) { inks.push_back({ w, r, t, pic }); };
    if (auto *l = qobject_cast<QLabel *>(w)) {
        if (!l->pixmap().isNull()) return;   /* a picture: the templates draw fields / axis letters on their pictures (by design) */
        QString t = l->text(); if (t.trimmed().isEmpty()) return;
        QRect cr = l->contentsRect().adjusted(l->margin(), l->margin(), -l->margin(), -l->margin());
        int ind = l->indent() < 0 ? (l->frameWidth() > 0 ? fm.horizontalAdvance('x') / 2 : 0) : l->indent();
        if (ind > 0) { if (l->alignment() & Qt::AlignLeft) cr.setLeft(cr.left() + ind); else if (l->alignment() & Qt::AlignRight) cr.setRight(cr.right() - ind); }
        QRect ink;
        bool rich = l->textFormat() == Qt::RichText || (l->textFormat() == Qt::AutoText && Qt::mightBeRichText(t));
        if (rich) {
            QTextDocument d; d.setDefaultFont(l->font()); d.setHtml(t); d.setDocumentMargin(0);
            if (l->wordWrap()) d.setTextWidth(cr.width());
            QSize s(qCeil(d.idealWidth()), qCeil(d.size().height()));
            ink = QStyle::alignedRect(Qt::LeftToRight, l->alignment(), s, cr);
            t = d.toPlainText();
        } else {
            int al = l->alignment() | (l->wordWrap() ? Qt::TextWordWrap : 0);
            QString shown = l->textInteractionFlags() & Qt::TextShowMnemonic ? t : t;
            ink = inkV(fm, fm.boundingRect(cr, al | (l->buddy() ? Qt::TextShowMnemonic : 0), shown));
            if (l->buddy()) t = plain(t);
        }
        inkOf(ink, t);
        QRect have = cr & vis;
        if (exceeds(ink, have)) report("label", tl, w, t, ink, have);
        return;
    }
    if (auto *g = qobject_cast<QGroupBox *>(w)) {
        if (g->title().isEmpty() && !g->isCheckable()) return;
        QStyleOptionGroupBox o; o.initFrom(g); o.text = g->title(); o.lineWidth = 1; o.midLineWidth = 0; o.textAlignment = g->alignment();
        o.subControls = QStyle::SC_GroupBoxFrame | (g->title().isEmpty() ? QStyle::SC_None : QStyle::SC_GroupBoxLabel) | (g->isCheckable() ? QStyle::SC_GroupBoxCheckBox : QStyle::SC_None);
        o.features = g->isFlat() ? QStyleOptionFrame::Flat : QStyleOptionFrame::None;
        QRect lab = st->subControlRect(QStyle::CC_GroupBox, &o, QStyle::SC_GroupBoxLabel, g);
        if (g->isCheckable()) lab |= st->subControlRect(QStyle::CC_GroupBox, &o, QStyle::SC_GroupBoxCheckBox, g);
        QRect ink(lab.left(), lab.top(), std::max(lab.width(), fm.horizontalAdvance(plain(g->title()))), lab.height());
        inkOf(ink, plain(g->title()));
        if (exceeds(ink, vis)) report("groupbox", tl, w, plain(g->title()), ink, vis);
        return;
    }
    if (auto *b = qobject_cast<QAbstractButton *>(w)) {
        QString t = plain(b->text());
        if (auto *tb = qobject_cast<QToolButton *>(w)) {
            if (t.isEmpty() || tb->toolButtonStyle() == Qt::ToolButtonIconOnly || (tb->toolButtonStyle() == Qt::ToolButtonFollowStyle && !tb->icon().isNull())) { inkOf(tb->rect(), QString(), true); return; }
            QSize sh = tb->sizeHint(); QRect ink = QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, sh, tb->rect());
            QRect have = tb->rect() & vis;
            inkOf(tb->rect(), t);
            const int tol = tb->inherits("SARibbonToolButton") ? 5 : TOL;   /* SARibbon's size hint keeps 4 px of slack past its drawn text */
            if (sh.width() > tb->width() + tol || (sh.width() > have.width() + tol)) report("toolbutton", tl, w, t, ink, have);
            return;
        }
        if (t.isEmpty()) {                             /* no text: the indicator of a check box / radio, a button's frame */
            QStyleOptionButton o; o.initFrom(b);
            if (qobject_cast<QCheckBox *>(w)) inkOf(st->subElementRect(QStyle::SE_CheckBoxIndicator, &o, b), QString(), true);
            else if (qobject_cast<QRadioButton *>(w)) inkOf(st->subElementRect(QStyle::SE_RadioButtonIndicator, &o, b), QString(), true);
            else inkOf(b->rect(), QString(), true);
            return;
        }
        QStyleOptionButton o; o.initFrom(b); o.text = b->text(); o.icon = b->icon(); o.iconSize = b->iconSize();
        int iw = b->icon().isNull() ? 0 : b->iconSize().width() + 4;
        if (qobject_cast<QCheckBox *>(w) || qobject_cast<QRadioButton *>(w)) {
            bool cbx = qobject_cast<QCheckBox *>(w) != nullptr;
            QRect cr = st->subElementRect(cbx ? QStyle::SE_CheckBoxContents : QStyle::SE_RadioButtonContents, &o, b);
            QRect ind = st->subElementRect(cbx ? QStyle::SE_CheckBoxIndicator : QStyle::SE_RadioButtonIndicator, &o, b);
            QRect ink = b->property("textfit_multiline").toBool() ? inkV(fm, fm.boundingRect(cr, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, t))
                                                                  : textRect(fm, cr.adjusted(iw, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, t);
            if (iw) ink.setLeft(cr.left());
            inkOf(ink | ind, t);
            QRect have = cr & vis;
            if (exceeds(ink, have)) report(cbx ? "checkbox" : "radio", tl, w, t, ink, have);
            return;
        }
        int fw = st->pixelMetric(QStyle::PM_DefaultFrameWidth, &o, b);
        QRect area = b->rect().adjusted(fw, fw, -fw, -fw);
        QRect ink = textRect(fm, area, Qt::AlignCenter, t);
        if (iw) { ink.adjust(-iw / 2, 0, iw - iw / 2, 0); }
        inkOf(b->rect(), t);
        QRect have = area & vis;
        if (exceeds(ink, have)) report("button", tl, w, t, ink, have);
        return;
    }
    if (auto *cb = qobject_cast<QComboBox *>(w)) {
        QStyleOptionComboBox o; o.initFrom(cb); o.editable = cb->isEditable(); o.frame = cb->hasFrame(); o.currentText = cb->currentText();
        QRect field = st->subControlRect(QStyle::CC_ComboBox, &o, QStyle::SC_ComboBoxEditField, cb);
        QRect have = field & vis;
        inkOf(field, cb->currentText());   /* its text field (frames touching are not text overlapping) */
        for (int i = 0; i < cb->count(); i++) {
            QString t = cb->itemText(i); if (t.isEmpty()) continue;
            int iw = cb->itemIcon(i).isNull() ? 0 : cb->iconSize().width() + 4;
            QRect ink = textRect(fm, field.adjusted(iw, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, t);
            if (iw) ink.setLeft(field.left());
            if (exceeds(ink, have)) report(i == cb->currentIndex() ? "combo" : "combo-item", tl, w, t, ink, have);
        }
        if (cb->isEnabled() && cb->count() > 0 && !getenv("MCAMUI_TEXTCHECK_NOPOPUP")) {   /* the popup list as shown (QComboBox::showPopup, our filter) */
            cb->showPopup();
            QAbstractItemView *v = cb->view();
            int vw = v->viewport()->width();
            for (int i = 0; i < cb->count(); i++) {
                QString t = cb->itemText(i); if (t.isEmpty()) continue;
                QModelIndex ix = cb->model()->index(i, cb->modelColumn(), cb->rootModelIndex());
                int need = itemNeed(cb, i); (void)ix;
                if (need > vw + TOL) report("popup", tl, w, t, QRect(0, 0, need, 0), QRect(0, 0, vw, 0));
            }
            cb->hidePopup();
        }
        return;
    }
    if (auto *e = qobject_cast<QLineEdit *>(w)) {
        if (e->text().isEmpty() || e->echoMode() != QLineEdit::Normal) { inkOf(e->rect(), QString(), true); return; }
        QStyleOptionFrame o; o.initFrom(e); o.lineWidth = e->hasFrame() ? st->pixelMetric(QStyle::PM_DefaultFrameWidth, &o, e) : 0;
        QRect cr = st->subElementRect(QStyle::SE_LineEditContents, &o, e);
        QMargins m = e->textMargins(); cr = cr.marginsRemoved(m).adjusted(2, 0, -2, 0);   /* QLineEdit's horizontal margin 2 */
        QRect ink = textRect(fm, cr, Qt::AlignLeft | Qt::AlignVCenter, e->text());
        ink = QRect(ink.left(), ink.top(), fm.horizontalAdvance(e->text()), ink.height());
        inkOf(cr, e->text());
        QRect have = cr & vis;
        if (ink.right() > have.right() + TOL) report("edit", tl, w, e->text(), ink, have);
        return;
    }
    if (auto *tb = qobject_cast<QTabBar *>(w)) {
        for (int i = 0; i < tb->count(); i++) {
            QRect r = tb->tabRect(i); if (r.isEmpty()) continue;
            QSize sh = static_cast<TB *>(tb)->tabSizeHint(i);
            bool vert = tb->shape() == QTabBar::RoundedWest || tb->shape() == QTabBar::RoundedEast || tb->shape() == QTabBar::TriangularWest || tb->shape() == QTabBar::TriangularEast;
            int need = vert ? sh.height() : sh.width(), have = vert ? r.height() : r.width();
            QRect hv = r & vis;
            if (need > have + TOL || (vert ? hv.height() : hv.width()) + TOL < need) report("tab", tl, w, plain(tb->tabText(i)), QRect(0, 0, need, 0), QRect(0, 0, std::min(have, vert ? hv.height() : hv.width()), 0));
        }
        return;
    }
    if (auto *h = qobject_cast<QHeaderView *>(w)) {
        if (!h->model()) return;
        for (int i = 0; i < h->count(); i++) {
            if (h->isSectionHidden(i) || h->sectionSize(i) <= 0) continue;
            QString t = h->model()->headerData(i, h->orientation(), Qt::DisplayRole).toString(); if (t.isEmpty()) continue;
            QSize s = static_cast<HV *>(h)->sectionSizeFromContents(i);
            int need = h->orientation() == Qt::Horizontal ? s.width() : s.height();
            if (h->isSortIndicatorShown() && h->sortIndicatorSection() != i) need -= st->pixelMetric(QStyle::PM_HeaderMarkSize, nullptr, h);   /* the arrow only on the sorted one */
            int pos = h->sectionViewportPosition(i), sz = h->sectionSize(i);
            int shown = std::min(pos + sz, h->viewport()->width()) - std::max(pos, 0);
            if (h->orientation() == Qt::Horizontal && pos < h->viewport()->width() && pos + sz > 0 && (need > sz + TOL)) report("header", tl, w, t, QRect(0, 0, need, 0), QRect(0, 0, std::min(sz, shown), 0));
        }
        return;
    }
    if (auto *v = qobject_cast<QAbstractItemView *>(w)) {
        QAbstractItemModel *m = v->model(); if (!m) return;
        QRect vp = v->viewport()->rect();
        std::function<void(const QModelIndex &, int)> walk = [&](const QModelIndex &par, int depth) {
            int rows = m->rowCount(par), cols = m->columnCount(par);
            for (int r = 0; r < rows && r < 2000; r++) {
                bool anyVis = false;
                for (int c = 0; c < cols; c++) {
                    QModelIndex ix = m->index(r, c, par);
                    QRect vr = v->visualRect(ix);
                    if (vr.isEmpty() || !vr.intersects(vp)) continue;
                    anyVis = true;
                    QString t = ix.data(Qt::DisplayRole).toString(); if (t.isEmpty()) continue;
                    QStyleOptionViewItem o; static_cast<AV *>(v)->initViewItemOption(&o); o.rect = vr;
                    QSize sh = v->itemDelegateForIndex(ix)->sizeHint(o, ix);
                    if (sh.width() > vr.width() + TOL) report("item", tl, w, t, QRect(0, 0, sh.width(), sh.height()), QRect(0, 0, vr.width(), vr.height()));
                }
                if (anyVis && depth < 8 && m->hasChildren(m->index(r, 0, par))) walk(m->index(r, 0, par), depth + 1);
            }
        };
        walk(v->rootIndex(), 0);
        return;
    }
}

void checkTree(QWidget *p, QWidget *tl) {
    QVector<Ink> inks;
    for (QObject *o : p->children()) {
        QWidget *c = qobject_cast<QWidget *>(o);
        if (!c || c->isWindow() || !c->isVisible()) continue;
        if (visibleRect(c, tl).isEmpty()) continue;
        checkWidget(c, tl, inks);
        if (!qobject_cast<QComboBox *>(c)) checkTree(c, tl);
    }
    /* siblings whose drawn text runs into each other (a text widget against a text widget or a picture / a field) */
    for (int i = 0; i < inks.size(); i++)
        for (int j = i + 1; j < inks.size(); j++) {
            const Ink &a = inks[i], &b = inks[j];
            if (a.text.isEmpty() && b.text.isEmpty()) continue;
            QRect ra = a.r.translated(a.w->pos()), rb = b.r.translated(b.w->pos());
            QRect x = ra & rb;
            if (x.width() <= 3 || x.height() <= 3) continue;   /* a frame / descender touching: not text over text */
            const Ink &t = a.text.isEmpty() ? b : a, &o = a.text.isEmpty() ? a : b;
            report("overlap", tl, t.w, t.text, ra, rb, QString("with %1 \"%2\" %3,%4 %5x%6").arg(o.w->metaObject()->className(), esc(o.text)).arg(x.x()).arg(x.y()).arg(x.width()).arg(x.height()));
        }
}
}

void check(const char *where) {
    if (!enabled()) return;
    if (!g_out) {
        QByteArray f = qgetenv("MCAMUI_TEXTCHECK");
        g_out = f == "-" || f == "1" ? stderr : fopen(f.constData(), "a");
        if (!g_out) g_out = stderr;
    }
    g_where = where;
    QApplication::processEvents();
    for (QWidget *tl : QApplication::topLevelWidgets()) {
        if (!tl->isVisible() || qobject_cast<QMenu *>(tl) || tl->inherits("QComboBoxPrivateContainer") || tl->objectName() == "qtooltip_label") continue;
        checkTree(tl, tl);
    }
    fflush(g_out);
}

/* every page of the open windows (not the main window): each tab of a tab bar, each item of a page tree (the op dialog's
   pages, the Lathe Face sheet, the stock / tool dialogs ...), the check after each; the first page shown again at the end */
/* tests: MCAMUI_TEXTCHECK_SHOTS=DIR - each page checked saved as DIR/WINDOW_N.png (a look at what the checker reports) */
static void pageShot(QWidget *tl, const QString &wh) {
    static int n = 0; QByteArray d = qgetenv("MCAMUI_TEXTCHECK_SHOTS"); if (d.isEmpty()) return;
    QString f = QString("%1/%2_%3_%4.png").arg(QString::fromUtf8(d), tl->objectName()).arg(QCoreApplication::applicationPid()).arg(n++, 3, 10, QChar(0x30));
    tl->grab().save(f); fprintf(stderr, "pageshot %s %s\n", f.toUtf8().constData(), wh.toUtf8().constData());
}
void checkPages(const char *where) {
    if (!enabled()) return;
    for (QWidget *tl : QApplication::topLevelWidgets()) {
        if (!tl->isVisible() || qobject_cast<QMainWindow *>(tl) || qobject_cast<QMenu *>(tl)) continue;
        for (QTabBar *tb : tl->findChildren<QTabBar *>()) {
            if (!tb->isVisible()) continue;
            int cur = tb->currentIndex();
            for (int i = 0; i < tb->count(); i++) {
                if (!tb->isTabEnabled(i) || !tb->isTabVisible(i)) continue;
                tb->setCurrentIndex(i); QApplication::processEvents();
                { QString wh = QString("%1 tab %2").arg(where, plain(tb->tabText(i))).toUtf8().constData(); check(wh.toUtf8().constData()); pageShot(tl, wh); }
            }
            tb->setCurrentIndex(cur); QApplication::processEvents();
        }
        for (QTreeWidget *t : tl->findChildren<QTreeWidget *>()) {
            if (!t->isVisible()) continue;
            QPointer<QTreeWidget> tp(t);
            auto items = [&] { t->expandAll(); return t->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive); };
            int n = items().size();
            for (int i = 0; i < n && tp && t->isVisible(); i++) {
                QList<QTreeWidgetItem *> all = items(); if (i >= all.size()) break;
                QTreeWidgetItem *it = all[i];
                if (!(it->flags() & Qt::ItemIsEnabled) || !(it->flags() & Qt::ItemIsSelectable)) continue;
                QString txt = it->text(0);
                t->setCurrentItem(it); emit t->itemClicked(it, 0); QApplication::processEvents();
                { QString wh = QString("%1 page %2").arg(where, txt).toUtf8().constData(); check(wh.toUtf8().constData()); pageShot(tl, wh); }
            }
        }
    }
}
}
