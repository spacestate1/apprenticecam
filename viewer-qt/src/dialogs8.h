// dialogs8 - the remaining phase 8 dialogs in Qt, laid out from their decoded templates (chainDu: dialog units with the
// templates' 6 x 13 base units) and bound to core_ui (viewer/core_ui/cui_analyze.c, cui_ecp.c, cui_misc.c):
//   AnalyzeDialog  mccadutilres 1073 Point / 1080 Line / 6470 Arc Properties (formats/analyze.md)
//   ColorPicker    opmanres 593 "Colors" + page 106 (CColorDlg / CColorPage1; Customize 108 greyed)
//   LevelPicker    opmanres 563 "Levels" (CSelLevel picker, formats/level_picker.md)
//   EcpDialog      opmanres 243 "Edit Common Parameters (Mill/Router)" (CEditCommonDlg)
//   GridDialog     mccoreres 1589 "Configuration - Screen Grid" (the page only)
//   AboutWindow, KeysWindow  ours (formats/provenance_audit.md "Intentional deviations": About apprenticeCAM, OnHelp keymap list)
// viewer/mcamui.c draws the same models with nuklear.
#pragma once
#include "chaindlg.h"
#include <functional>
class QLineEdit; class QLabel; class QToolButton; class QPushButton; class QCheckBox; class QRadioButton; class QComboBox;
class QListWidget; class QTreeWidget; class QTabBar; class QTableWidget;

class AnalyzeDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit AnalyzeDialog(QWidget *parent);
    void refresh();                                    // rebuilt for the entity type / single list, then the field texts
    void dump() const;
    bool testSet(const QString &key, const QString &v);
    std::function<void(const std::function<void()> &)> withGL;
signals:
    void changed();
private:
    void rebuild();
    struct Ctl { const BsCtl *c; QWidget *w; };
    QVector<Ctl> ctls_; const BsCtl *tmpl_ = nullptr; int single_ = -1; bool filling_ = false;
};

class ColorGrid;
class ColorPicker : public FloatDlg {
    Q_OBJECT
public:
    explicit ColorPicker(QWidget *parent);
    void refresh();
    void dump() const;
    bool testSet(const QString &key, const QString &v);
signals:
    void changed();
private:
    QLineEdit *cur_ = nullptr; QLabel *swatch_ = nullptr; QPushButton *sel_ = nullptr; ColorGrid *grid_ = nullptr; bool filling_ = false;
};

class LevelPicker : public FloatDlg {
    Q_OBJECT
public:
    explicit LevelPicker(QWidget *parent);
    void refresh();
    void dump() const;
    bool testSet(const QString &key, const QString &v);
signals:
    void changed();
private:
    QTreeWidget *list_ = nullptr; QLineEdit *num_ = nullptr, *name_ = nullptr, *lo_ = nullptr, *hi_ = nullptr;
    QRadioButton *radio_[4] = {}; QToolButton *pick_ = nullptr; bool filling_ = false;
};

class EcpDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit EcpDialog(QWidget *parent);
    void refresh();
    void dump() const;
    bool testSet(const QString &key, const QString &v);
    std::function<void(const std::function<void()> &)> withGL;
signals:
    void changed();
private:
    struct Ctl { const EcpCtl *c; QWidget *w; };
    QVector<Ctl> ctls_; QListWidget *ops_ = nullptr; bool filling_ = false;
    QTableWidget *tools_ = nullptr; QVector<int> toolRows_, toolCols_;   // list 395 (core_ui ecp_tool_rows / ecp_tool_cols): row -> part tool, shown columns
};

class GridDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit GridDialog(QWidget *parent);
    void refresh();
    bool testSet(const QString &key, const QString &v);
signals:
    void changed();
private:
    QLineEdit *ed_[5] = {}; QRadioButton *near_ = nullptr, *always_ = nullptr; bool filling_ = false;
};

/* opmanres dialog 977 "Warning: Dirty Operations Selected" (CDirtyOpsDlg, core_ui cui_dirty.c dop_*) */
class DirtyOpsDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit DirtyOpsDialog(QWidget *parent);
    void refresh();
signals:
    void answered(int ok);
private:
    QRadioButton *regen_ = nullptr, *noregen_ = nullptr; bool filling_ = false;
};

class AboutWindow : public FloatDlg {
    Q_OBJECT
public:
    explicit AboutWindow(QWidget *parent);
signals:
    void changed();
};

class KeysWindow : public FloatDlg {
    Q_OBJECT
public:
    KeysWindow(QWidget *parent, const std::function<QString(const QString &)> &label);
    void refresh();
signals:
    void changed();
private:
    QTreeWidget *list_ = nullptr; QLabel *src_ = nullptr; std::function<QString(const QString &)> label_;
};
