// lfacedlg - the new Lathe Face sheet (mclathe edit_lathe_operation 0x1801c7480: CLTpSheet with the tool page 3911 and the Face page
// 5361) at its template positions; every control is core_ui cui_lface.c's (LF_CTLS: role, binding). formats/lathe.md 8.
#pragma once
#include "chaindlg.h"
#include <functional>
#include <QVector>
class QTabBar; class QTableWidget;

class LfaceDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit LfaceDialog(QWidget *parent);
    void refresh();
    std::function<void(const std::function<void()> &)> withGL;
signals:
    void changed();
private:
    struct Ctl { const LfCtl *c; QWidget *w; };
    void run(const std::function<void()> &fn);
    QVector<Ctl> ctls_; QWidget *page_[2] = {}; QTabBar *tabs_ = nullptr; QTableWidget *list_ = nullptr; bool filling_ = false;
};
