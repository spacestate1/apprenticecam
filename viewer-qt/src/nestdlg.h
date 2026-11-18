// NestDialog: Transform > Geometry Nesting's windows (NESTING.DLL, formats/nesting_addin.md) drawn from core_ui's template
// tables (cui_nest.c ns_table: the property sheet CNestPropSht with its Sheets 101 / Parts 105 pages, the Configuration
// dialog CCfgDlg 201 and the Nesting Results dialog CResultDlg 205) and the ns_* model. One class per window kind: the
// template id picks the table.
#pragma once
#include "chaindlg.h"
#include <QMap>
#include <functional>
class QTabBar;
class NestDialog : public FloatDlg {
    Q_OBJECT
public:
    NestDialog(int tpl, QWidget *parent);                 // tpl NS_SHEET (the property sheet with its pages), 201, 205
    void refresh();
    bool testSet(const QString &key, const QString &v);   // a control through its widget (nestset:ID=V, tab=K, menu:LIST,CMD)
    void dump() const;
    std::function<void(const std::function<void()> &)> withGL;   // the graphics context for the commands that change the part
signals:
    void changed();
private:
    void build(int tpl, QWidget *page);
    void fill(int tpl, unsigned id, QWidget *w);
    void popupMenu(int tpl, unsigned id, QPoint global);
    void gl(const std::function<void()> &f) { if (withGL) withGL(f); else f(); }
    int tpl_;
    QTabBar *tabs_ = nullptr;
    QWidget *pages_[2] = { nullptr, nullptr };
    QMap<unsigned, QWidget *> w_[3];                      // [0] the sheet's own / the dialog, [1] Sheets 101, [2] Parts 105
    bool filling_ = false;
};
