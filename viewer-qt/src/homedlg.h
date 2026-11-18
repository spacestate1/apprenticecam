// homedlg.h - the Home tab's Attributes / Organize dialogs (core_ui cui_attr.c): Change Levels (opmanres 516), Attributes (192),
// and the bitmap combos of the line style / point style / line width (CBmpCombo2, opmanres bitmaps).
#pragma once
#include "chaindlg.h"
class QComboBox; class QLineEdit; class QCheckBox; class QRadioButton; class QPushButton;

// kind 0 line style (opmanres bitmaps 0x61b7..0x61bb), 1 line width (0x61bd..0x61c1), 2 point style (0x61c9..0x61d0); item data = the
// 1-based value (CBmpCombo2::AddLineStyleImages / AddLineWidthImages / AddPointStyleImages)
QComboBox *makeAttrCombo(QWidget *parent, int kind);
void setAttrCombo(QComboBox *c, int value);

class ChangeLevelsDlg : public FloatDlg {
    Q_OBJECT
public:
    explicit ChangeLevelsDlg(QWidget *parent);
    void start();                       // the dialog for the selection (FUN_18046b290's init)
    void dump() const;
signals:
    void changed();
private:
    void finish(bool ok);
    QRadioButton *move_ = nullptr, *copy_ = nullptr; QCheckBox *active_ = nullptr; QLineEdit *num_ = nullptr; QPushButton *sel_ = nullptr;
};

class AttribsDlg : public FloatDlg {
    Q_OBJECT
public:
    explicit AttribsDlg(QWidget *parent);
    void start();
    void dump() const;
signals:
    void changed();
private:
    void finish(bool ok);
    QCheckBox *useColor_ = nullptr, *useLn_ = nullptr, *usePt_ = nullptr, *useLevel_ = nullptr, *useW_ = nullptr;
    QLineEdit *color_ = nullptr, *level_ = nullptr; QComboBox *ln_ = nullptr, *pt_ = nullptr, *w_ = nullptr;
};
