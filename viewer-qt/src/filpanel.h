// filpanel - Solids > Modify > Constant Fillet: the function panel (MCGeomSld WPF SolidsFilletManagerView, title sld 823 "Constant Radius
// Fillet", formats/solid_fillet.md 2) and, while the entities are picked, the Solid Selection dialog (mcgeomsldres 1413: Edge / Face / Body,
// UnSelect All, OK; solid_fillet.md 1.2) as Qt widgets in one dock. The model, every rule and every action are core_ui's
// (viewer/core_ui/cui_fillet.c: fl_p, fl_validate, fl_set_*, fl_accept ...); this file only shows it.
#pragma once
#include <QWidget>

class QLabel; class QLineEdit; class QCheckBox; class QComboBox; class QPushButton; class QListWidget; class QTabBar; class QStackedWidget;

class FilletPanel : public QWidget {
    Q_OBJECT
public:
    explicit FilletPanel(QWidget *parent = nullptr);
    void refresh();                                    // values / enabled states / errors from the model
signals:
    void changed();                                    // the model changed (preview + redraw)
    void button(int code);                             // 0 OK, 1 OK-New / OK-Regen, 2 Cancel, 3 End Selection
private:
    void edited();
    bool filling_ = false;
    QStackedWidget *mode_ = nullptr;                   // 0 the Solid Selection dialog, 1 the panel
    // Solid Selection (1413)
    QPushButton *bEdge_, *bFace_, *bBody_, *unselAll_, *selOk_; QLabel *selCount_;
    // panel
    QTabBar *tabs_; QStackedWidget *pages_;
    QLabel *nameLab_; QLineEdit *name_; QCheckBox *prop_, *mitre_; QComboBox *overflow_;
    QListWidget *list_; QPushButton *addSel_, *reselAll_;
    QLabel *radLab_; QLineEdit *radius_;
    QCheckBox *autoPrev_; QPushButton *prevBtn_;
    QLabel *warn_;
    QPushButton *ok_, *okNew_, *cancel_;
};
