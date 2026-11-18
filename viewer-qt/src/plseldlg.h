// plseldlg - the Plane Selection dialog (mccoreres dialog 1085, mccore CPlaneDlg; the loop is get_plane_info 0x180a2aea0): every
// control is core_ui cui_plsel.c's (PSEL1085); formats/plane_selection.md. Shown while psel_open (hidden while its X / Y / Z
// point pick runs: the dialog is suspended there).
#pragma once
#include "chaindlg.h"
#include <functional>
#include <QHash>
class QLineEdit; class QAbstractButton; class QToolButton;

class PlaneSelDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit PlaneSelDialog(QWidget *parent);
    void refresh();
    void dump() const;
    bool testSet(const QString &key, const QString &v);   // tests: btn=ID (a click), edit=I:TEXT (typed + focus out), spin=I:up|dn
    std::function<void(const std::function<void()> &)> withGL;
signals:
    void changed();
private:
    void run(const std::function<void()> &fn);
    QLineEdit *edit_[3] = {}; QToolButton *spin_[3][2] = {}; QHash<int, QAbstractButton *> btn_; int upd_ = -1; bool filling_ = false; int flipShown_ = -1;
};
