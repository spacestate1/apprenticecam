#pragma once
/* Removing widgets that a model-driven rebuild replaces. A rebuild often runs from a control's own signal
   (QLineEdit::editingFinished is emitted inside its focusOutEvent): deleting that control there frees it while Qt is
   still in its handler and leaves QApplication's focus widget dangling (the next click crashed in
   QWidget::focusOutEvent). So: silence the old widgets (hiding a focused edit would emit editingFinished again and
   re-enter the rebuild), hide them, and delete them once control is back in the event loop. */
#include <QWidget>

inline void retireWidget(QWidget *w) {
    if (!w) return;
    w->blockSignals(true);
    for (QObject *o : w->findChildren<QObject *>()) o->blockSignals(true);
    w->hide();
    w->setParent(nullptr);
    w->deleteLater();
}
