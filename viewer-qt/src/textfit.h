// textfit - ours: text never clipped / overlapping (user request 2026-11-17 "make sure text never overlaps anywhere in drop
// downs etc"). Two parts:
//  1. TextFit::install(app): the shared fixes, an application event filter (combo popups at least as wide as their longest
//     item; see textfit.cpp).
//  2. TextFit::check(where): the checker (tests: MCAMUI_TEXTCHECK=FILE appends a report line per text that does not fit:
//     every visible top-level window's widgets; text width / height from the widget's own QFontMetrics against the rectangle
//     the style draws it in, clipped by the ancestors; text-bearing siblings whose drawn text overlaps; every combo item
//     against its closed field and its popup list). tools/tests/ui_text_check.sh runs it over the GUI scenes.
#pragma once
class QApplication; class QTableView; class QWidget;
#include <QColor>
#include <QPixmap>
#include <QFont>
#include <QString>
namespace TextFit {
void install(QApplication *app);
QFont dialogFont(QFont base = QFont());   // the dialog templates' font (UI-4: a metric match of MS Sans Serif 8 / Tahoma 8)
QString dialogFontCss();                 // the same as style sheet properties ("font-size: ..; font-family: ..;")
void fitColumns(QTableView *t);           // UI-2 / UI-5: every column at least as wide as its header and cells (the list scrolls sideways)
extern const QColor MARKER_COLOR;            // UI-3: the colour of this program's own / NOT DECODED controls (rgb(150, 90, 0), made readable)
void markOurs(QWidget *w, const QString &tip);   // UI-3: W's text in the marker colour, TIP its tooltip
QPixmap keyedBitmap(const QString &packName, unsigned key);   // a bitmap of the icon pack with colour KEY (0xRRGGBB) clear (null: not in the pack)
bool on();                            // the fixes on (MCAMUI_TEXTFIT_OFF unset)
bool enabled();                       // MCAMUI_TEXTCHECK set
void check(const char *where);        // no-op unless enabled()
void checkPages(const char *where);   // every tab / page-tree item of the open dialogs, checked (the textcheck:pages step)
}
