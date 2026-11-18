// toolmgr - the Tool Manager (mccoreres dialog 1099, CToolListBoth), the Tool List Filter (25519, CToolFilt), the tool
// report window (the Toolpaths manager's Tool list... and the Tool Manager's Report... / Detailed report...) and Renumber
// tools (opmanres 805, CRenumbTl), as Qt widgets over core_ui's model (viewer/core_ui/cui_toolmgr.c: selection, sort, filter,
// prompts, the library file rules, the actions, menu 1225, the OnSize layout tm_layout, the reports, Renumber;
// cui_toolpage.c: the column sets / cells / the current library). viewer/toolmgr_ui.inc + mcamui.c draw the same with
// nuklear. Positions: tm_layout in pixels from the template's dialog units (chainDu's 6 x 13 base units, 11 px font).
#pragma once
#include "chaindlg.h"
#include <QVector>
#include <functional>

class QTableWidget; class QListWidget; class QLabel; class QPushButton; class QToolButton; class QComboBox; class QCheckBox;
class QRadioButton; class QGroupBox; class QFrame; class QMenu; class QLineEdit; class QScrollArea;

class ToolManagerDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit ToolManagerDialog(QWidget *parent);
    void refresh(bool force = false);                  // from the model (when it changed)
    bool keyDown(int qtKey, bool ctrl);                // the dialog is modal: every key comes here (mcamui tm_keydown)
    QMenu *popupMenu(int L, const QPoint &global);     // menu 1225 (core_ui tm_menu_items)
    QMenu *columnMenu(int L, const QPoint &global);    // VisibleColumnPopupMenu
    void resizeBody(int w, int h);                     // WM_SIZING (the minimum is the template size)
    void dump() const;                                 // tests: the widgets
    bool testSet(const QString &key, const QString &v);   // tests: a control through its widget
signals:
    void changed();                                    // the model changed (the main window refreshes the tree / other dialogs)
protected:
    bool eventFilter(QObject *o, QEvent *e) override;
private:
    void relayout();
    void fillList(int L);
    void clickRow(int L, int row, Qt::KeyboardModifiers km, bool right);
    void fileAct(int what);                            // New / Browse / Import / Save tool(s) to / Compress: the file dialogs
    void act(const std::function<void()> &fn);         // run a model action, then refresh + changed
    QComboBox *group_ = nullptr, *view_ = nullptr;
    QLabel *bmp_ = nullptr, *used_ = nullptr, *plbl_ = nullptr, *llbl_ = nullptr, *path_ = nullptr;
    QCheckBox *fa_[2] = {};
    QPushButton *filt_ = nullptr, *ok_ = nullptr, *help_ = nullptr;
    QToolButton *up_ = nullptr, *down_ = nullptr, *bnew_ = nullptr, *bbrw_ = nullptr, *bimp_ = nullptr;
    QGroupBox *grp_ = nullptr; QRadioButton *rad_[3] = {};
    QTableWidget *tab_[2] = {}; QListWidget *ico_[2] = {};
    QVector<int> rows_[2];                             // the shown order (tm_rows)
    QVector<int> cols_[2];                             // the Details columns shown (in order)
    QFrame *prompt_ = nullptr; QLabel *promptText_ = nullptr; QCheckBox *promptDont_ = nullptr; QPushButton *promptYes_ = nullptr, *promptNo_ = nullptr;
    QWidget *grip_ = nullptr;
    bool filling_ = false;
    QByteArray sig_;
};

class ToolFilterDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit ToolFilterDialog(QWidget *parent);
    void refresh();                                    // tmf_edit -> widgets
    void dump() const;
    bool testSet(const QString &key, const QString &v);
signals:
    void closed();
private:
    QVector<QToolButton *> types_; QComboBox *op_ = nullptr, *unit_ = nullptr, *dm_ = nullptr; QLineEdit *d1_ = nullptr, *d2_ = nullptr;
    QCheckBox *rad_[3] = {}, *mat_[6] = {};
    QPushButton *ok_ = nullptr, *cancel_ = nullptr;
    bool filling_ = false;
};

class ReportWindow : public FloatDlg {
    Q_OBJECT
public:
    explicit ReportWindow(QWidget *parent);
    void refresh();                                    // the laid-out report (core_ui tlr)
private:
    QScrollArea *scroll_ = nullptr; QWidget *pages_ = nullptr;
};

class RenumberDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit RenumberDialog(QWidget *parent);
    void refresh();                                    // rn_* -> widgets (rn_open filled them)
    void dump() const;
    bool testSet(const QString &key, const QString &v);
    std::function<void(const std::function<void()> &)> withGL;   // OK regenerates (scene_reupload)
signals:
    void closed();
private:
    QLineEdit *ed_[4] = {}; QCheckBox *len_ = nullptr, *dia_ = nullptr, *head_ = nullptr, *unused_ = nullptr;
    QPushButton *ok_ = nullptr, *cancel_ = nullptr;
    bool filling_ = false;
};
