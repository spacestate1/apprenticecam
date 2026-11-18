#pragma once
// regenprogress.h - the progress window and the input hold of the op dialog's OK while the toolpath is computed on a
// worker thread (opdlg.cpp OpDialog::ok). Ours: Mastercam's own regeneration progress UI is NOT DECODED.
#include <QProgressDialog>
#include <QPushButton>
#include <QKeyEvent>
#include <QCloseEvent>
#include <QWindow>
#include <QApplication>
#include <functional>
#include <climits>

// The window: Cancel button, Esc and the title bar's close all call onCancel; the window stays up (the computation
// stops at its next step) until the caller closes it. Not cancellable: the button shown disabled with a tooltip.
class RegenProgress : public QProgressDialog {
public:
    std::function<void()> onCancel;
    RegenProgress(QWidget *parent, bool cancellable, const QString &tip) : QProgressDialog(parent) {
        setWindowTitle("apprenticeCAM"); setLabelText("Regenerating toolpath...");   /* ours */
        setRange(0, 0); setAutoClose(false); setAutoReset(false);
        setMinimumDuration(INT_MAX);                   /* no automatic show: the caller shows it (QProgressDialog's own needs setValue) */
        setWindowModality(Qt::ApplicationModal);       /* when shown, Qt blocks every other window */
        QObject::disconnect(this, SIGNAL(canceled()), this, SLOT(cancel()));   /* Qt's own (string) connection: cancel() would hide it at once;
                                                                                 the button and the Esc QShortcut emit canceled() */
        connect(this, &QProgressDialog::canceled, this, [this] { fire(); });
        if (!cancellable) { auto *b = new QPushButton("Cancel"); b->setEnabled(false); b->setToolTip(tip); setCancelButton(b); }
        cancellable_ = cancellable;
    }
    void fire() { if (cancellable_ && onCancel) onCancel(); }
protected:
    void keyPressEvent(QKeyEvent *e) override { if (e->key() == Qt::Key_Escape) { e->accept(); fire(); return; } QProgressDialog::keyPressEvent(e); }
    void closeEvent(QCloseEvent *e) override { e->ignore(); fire(); }
    void reject() override { fire(); }
private:
    bool cancellable_ = true;
};

// The hold: every user input event (mouse, keys, wheel, shortcuts, drag and drop, window close) is dropped unless it
// goes to the progress window - a QWidget inside it or its QWindow (Qt delivers input to the QWidgetWindow first).
class InputHold : public QObject {
public:
    QWidget *allow = nullptr;
    bool eventFilter(QObject *o, QEvent *e) override {
        switch (e->type()) {
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick: case QEvent::MouseMove:
        case QEvent::KeyPress: case QEvent::KeyRelease: case QEvent::Wheel: case QEvent::Shortcut: case QEvent::ShortcutOverride:
        case QEvent::TouchBegin: case QEvent::TouchUpdate: case QEvent::TouchEnd: case QEvent::TabletPress: case QEvent::ContextMenu:
        case QEvent::DragEnter: case QEvent::DragMove: case QEvent::Drop: case QEvent::Close: {
            if (allow) {
                if (QWidget *w = qobject_cast<QWidget *>(o)) { if (w->window() == allow) return false; }
                else if (QWindow *win = qobject_cast<QWindow *>(o)) { if (win == allow->windowHandle()) return false; }
                else if (e->type() == QEvent::Shortcut || e->type() == QEvent::ShortcutOverride) {   /* a QShortcut / QAction: its parent widget */
                    QWidget *pw = qobject_cast<QWidget *>(o->parent()); if (pw && pw->window() == allow) return false; }
            }
            if (e->type() == QEvent::Close) e->ignore();
            return true; }
        default: return false;
        }
    }
};
