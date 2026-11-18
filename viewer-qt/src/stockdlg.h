// StockSetupDialog: Machine Group Properties (mastercam.dll EditGroupPropertiesDlg, opmanres 3851) with a mill group's three
// pages: Files (CGroupPg1, opmanres dialog 815), Tool Settings (CGroupPg2, 1589) and Stock Setup (CGroupPg3, 1590), each drawn
// from core_ui's template table at its template positions (core_ui cui_grpprops.c GP_815 / GP_1589, cui_stock.c ST_1590).
#pragma once
#include "chaindlg.h"
#include <QMap>
class QAbstractButton; class QLineEdit; class QLabel; class QTabBar; class QPushButton;
class StockSetupDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit StockSetupDialog(QWidget *parent);
    void refresh();
    bool testSet(const QString &key, const QString &v);   // a control of the shown page through its widget (stdlgset:ID=V, tab=K, ok, cancel)
    void dump() const;
signals:
    void changed();
private:
    void buildPage(int pg);
    QWidget *pages_[3] = { nullptr, nullptr, nullptr }; QTabBar *tabs_ = nullptr; QLabel *pic_ = nullptr; QWidget *marker_ = nullptr;
    QPushButton *ok_ = nullptr, *cancel_ = nullptr;
    QMap<unsigned, QWidget *> w_[3]; bool filling_ = false;
};
