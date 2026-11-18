// simwin - the Mastercam Simulator window (Verify), a copy of Mastercam 2017's UBVS window (ubvs.dll, WPF + Actipro ribbon;
// layout from mainwindow.baml, labels from UbvsResources MainWindowXaml.resx, icons ubvs_* from the user's icon pack) with
// apprenticeCAM's own stock-removal engine (viewer/verify.c, OURS) behind it. Spec: formats/simulator.md.
//   SimulatorWindow  the top-level window: ribbon (Home / View + the Verify context tab), the viewport, the slider row, the
//                    playback bar (Precision / VCR buttons / Speed), the Move List and Collision Report tool windows
//   SimViewport      the viewport: the shared scene drawn off-screen at this size (renderSceneImage, ours)
#pragma once
#include "SARibbonMainWindow.h"
#include "viewwidget.h"
#include <QHash>
#include <QPointer>

class QSlider; class QLabel; class QTableWidget; class QDockWidget; class QToolButton; class QCheckBox; class QAction;
class QComboBox; class QDoubleSpinBox;
class SARibbonCategory; class SARibbonContextCategory; class SARibbonPanel; class QMenu;

class SimViewport : public QWidget, public ViewApi {
    Q_OBJECT
public:
    SimViewport(ViewApi *main, QWidget *parent);
    void withGL(const std::function<void()> &fn) override { main_->withGL(fn); }
    void redraw() override { update(); }
    QImage grabView() override;
    QWidget *widget() override { return this; }
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
private:
    template <class F> void sized(F f);
    ViewApi *main_; ViewInput *in_;
};

class SimulatorWindow : public SARibbonMainWindow {
    Q_OBJECT
public:
    SimulatorWindow(ViewApi *mainView, QWidget *owner);
    void refresh();                                   // the controls from vf_* (after every change / play tick)
    void dump() const;                                // tests: the window's state (simdump step)
    bool testSet(const QString &id, const QString &v); // tests: press / set a control by its UBVS id (simset step)
    QImage grabWindow();                              // tests: the window alone (simshot step)
    SimViewport *viewport() { return vp_; }
signals:
    void changed();                                   // the main window redraws / refreshes
    void closedByUser();
protected:
    void closeEvent(QCloseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
private:
    QAction *act(const QString &id, const QString &label, const QString &icon, int size, const QString &tip, bool live);
    QAction *checkAct(const QString &id, const QString &label, const QString &icon, int size, const QString &tip, bool live, int *flag);
    QCheckBox *check(SARibbonPanel *p, const QString &id, const QString &label, const QString &tip, int *flag, bool enabled);
    void buildRibbon();
    void buildWorkspace();
    void buildDocks();
    void buildCompare();
    void fillCompare();
    void compareOptions();
    void run(int btn);
    ViewApi *main_; SimViewport *vp_ = nullptr;
    QSlider *pos_ = nullptr, *speed_ = nullptr, *prec_ = nullptr; QToolButton *vcr_[8] = {};
    QLabel *status_ = nullptr;
    QDockWidget *moves_ = nullptr, *coll_ = nullptr; QTableWidget *grid_ = nullptr;
    QDockWidget *cmp_ = nullptr; QTableWidget *cgrid_ = nullptr; QComboBox *csrc_ = nullptr; QDoubleSpinBox *cstl_ = nullptr;
    QToolButton *crefresh_ = nullptr, *cstock_ = nullptr; bool cmpShown_ = false;
    QHash<QString, QAction *> acts_; QHash<QString, QCheckBox *> checks_; QHash<QString, int *> flags_;
    SARibbonContextCategory *verifyCtx_ = nullptr;
    bool filling_ = false;
};
