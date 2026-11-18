// geopanel - the wireframe commands' function panels (mccad.ui.dll / mccadutil.ui.dll IWireframe*ManagerView, WPF
// FunctionPanel: OK, OK-New (Apply), Cancel, Help over the "Basic" tab; formats/ui_wireframe_panels.txt) as Qt
// widgets, docked in the managers' area (viewer/mcamui.c draw_manager + geopanel_ui.inc). The panel's contents and
// model are core_ui's (viewer/core_ui/cui_geo.c geo_panel_body, gps[geo_cmd], geo_panel_sync, pk_note): the body is
// walked once to create the widgets and again after every change to write the edited value through its pointer and to
// refresh values and enabled states (the walk is immediate-mode, as mcamui draws it every frame).
#pragma once
#include <QWidget>
#include <QVector>
#include <functional>

class QVBoxLayout; class QLabel; class QPushButton; class QScrollArea; class QTabBar;

class GeoPanel : public QWidget {
    Q_OBJECT
public:
    explicit GeoPanel(QWidget *parent = nullptr);
    void rebuild();                                    // a command started: its panel (geo_cmd)
    void refresh();                                    // values / enabled states from the model (fields being typed in keep their text)
    int command() const { return cmd_; }
    // tests (MCAMUI_SCRIPT panel / panelset:K=V / panellock:K=0|1): the controls, and a change made through a widget
    void dump() const;
    bool testSet(int k, const QString &v, bool lock);
    bool testFocus(int k);                             // tests: control k gets the keyboard focus (nothing typed)
    std::function<void(const std::function<void()> &)> withGL;   // run with the view's GL context (live rebuilds upload)
signals:
    void changed();                                    // a value changed (the model and the live result are updated)
    void button(int code);                             // 0 OK, 1 OK-New (Apply), 2 Cancel
public:
    struct Item;
    struct Pending { int k = -1; double d = 0; int i = 0; int lock = -1; QString s; };
    // builder state (used by the CuiPanelOps callbacks)
    int mode_ = 0;                                     // 0 build, 1 apply pending + refresh, 2 refresh
    int k_ = 0;                                        // item index of the walk
    QVector<Item *> items_;
    QVector<QVBoxLayout *> stack_;
    Pending pend_;
    void edited(int k, double d, int i, int lock);     // a widget's change: write through the walk, then sync
    QVector<QWidget *> tabw_;                          // the tabs' pages (CuiPanelOps tab), in tab order
    int tabIndex() const;
private:
    void walk(int mode);
    void syncEnabled();
    int cmd_ = 0;
    QPushButton *btn_[3] = {};                         // OK, Apply, Cancel
    QLabel *title_ = nullptr;
    QTabBar *tabs_ = nullptr;
    QWidget *body_ = nullptr; QScrollArea *scroll_ = nullptr;
};
