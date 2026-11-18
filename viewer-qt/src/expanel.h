// expanel - the Solids > Extrude function panel (MCGeomSld WPF SolidsExtrudeManagerView, title sld 846 "Solid Extrude",
// formats/extrude.md sec. 2) as Qt widgets, docked as an ADS tab in the managers' area while the panel is up (core_ui
// ex_mode && ex_phase; mcamui.c draw_extrude_panel / ex_page_basic / ex_page_advanced). The model, every rule and every
// action are core_ui's (viewer/core_ui/cui_extrude.c: ex_p, ex_enables, ex_validate, ex_set_*, ex_btn_*, ex_accept ...);
// this file only shows it: refresh() writes the model into the widgets, a widget's change goes through the core
// function and emits changed() (the main window then runs ex_panel_frame = the preview, and redraws).
#pragma once
#include <QWidget>
#include <QVector>

class QLabel; class QLineEdit; class QRadioButton; class QCheckBox; class QPushButton; class QToolButton;
class QListWidget; class QTabBar; class QStackedWidget; class QVBoxLayout;

class ExtrudePanel : public QWidget {
    Q_OBJECT
public:
    explicit ExtrudePanel(QWidget *parent = nullptr);
    void refresh();                                    // values / enabled states / errors from the model
    void dump() const;                                 // tests (MCAMUI_SCRIPT expanel): the controls
    bool testSet(const QString &key, const QString &v); // tests (expanelset:KEY=V): a change through the widget
signals:
    void changed();                                    // the model changed (preview + redraw)
    void button(int code);                             // 0 OK, 1 Apply / Regen, 2 Cancel
private:
    struct Exp { QToolButton *arrow; QWidget *hdr; QCheckBox *check; QWidget *box; int ix; };
    Exp expander(QVBoxLayout *page, const char *text, int ix, bool checkHeader);
    QLineEdit *value(QVBoxLayout *box, const char *label, QLabel **lab, double lo, double hi, double *var);
    void edited();                                     // after a model change: refresh + changed()
    bool filling_ = false;
    QTabBar *tabs_ = nullptr; QStackedWidget *pages_ = nullptr;
    QVector<Exp> exps_;
    // Basic: Operation
    QLabel *nameLab_; QLineEdit *name_; QRadioButton *create_, *cut_, *boss_; QLabel *targetLab_; QLineEdit *target_;
    QPushButton *selTarget_; QCheckBox *single_, *autoOp_;
    // Chains
    QListWidget *chains_; QPushButton *revAll_, *addCh_, *rechain_;
    // Distance
    QRadioButton *mDist_, *mThrough_; QLineEdit *dist_; QPushButton *autoCur_; QCheckBox *both_;
    // Trim to Faces
    QListWidget *faces_; QPushButton *addSel_, *reselAll_; QCheckBox *minDist_;
    // Advanced: Draft
    QLabel *angLab_; QLineEdit *ang_; QCheckBox *draftRev_, *split_, *draftEnds_;
    // Thin Wall
    QLabel *dirLab_; QRadioButton *d1_, *d2_, *dBoth_; QLabel *t1Lab_, *t2Lab_; QLineEdit *t1_, *t2_;
    // Plane Orientation
    QLineEdit *vec_; QPushButton *setNormal_, *setCplane_, *selVec_;
    // Preview
    QCheckBox *autoPrev_; QPushButton *prevBtn_;
    // FunctionPanel
    QPushButton *ok_, *apply_, *cancel_, *help_;
};
