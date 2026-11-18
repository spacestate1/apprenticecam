// managers - the left dock's manager tabs (viewer/mcamui.c draw_manager): Toolpaths (opmanres toolbars 800 / 804 +
// the operations tree, texts from viewer/optree_text.inc), Solids, Planes, Levels (viewer/levels_ui.inc),
// Recent Functions (MCCore.UI RecentFunctionsManagerView). Read-only in phase 1.
#pragma once
#include <QWidget>
#include <QStringList>
#include <functional>

class QTreeWidget; class QTreeWidgetItem; class QTableWidget; class QVBoxLayout; class QToolBar; class QMenu;
struct UiLayout;

struct CommandSink {
    std::function<bool(const QString &)> ok;     // a ported command (mcamui run_local_x probe)
    std::function<void(const QString &)> run;    // invoke it (by label)
};

class ToolpathsManager : public QWidget {
    Q_OBJECT
public:
    ToolpathsManager(const UiLayout &ui, CommandSink sink, QWidget *parent = nullptr);
    void refresh();
    void updateEnabled();
    int selected() const;
    void setOpExpanded(int op, bool on);
    void expandAll(bool on);                           // the menu's Expand (machine / toolpath group / every op) / Collapse (the ops)
    QMenu *popupMenu(const QPoint &global);            // the right-click menu (OPMANRES menu 780: core_ui cm_items)
    std::function<bool(int act)> menuHas;              // the front end has this menu action
    void beginRename(int group);                       // 3585 Rename: TVM_EDITLABEL on the group's item (core_ui ask: 2 machine, 1 toolpath group)
    bool clickPropsRow(int page);                      // tests (ours): a click on the Properties child of that page (itemClicked)
signals:
    void renamed();                                    // the label edit ended (core_ui ask_end)
    void selectionChanged();
    void paramsRow(int op);                            // the Parameters row / a double click: the op dialog (core_ui open_op_params)
    void toolRow(int op);                              // the tool row: the Tool Manager on that tool (mcamui draw_manager, phase 7)
    void propsRow(int page);                           // a Properties child: Machine Group Properties at that page (core_ui st_open)
    void menuRun(int item);                            // a menu item (core_ui cm_run)
private:
    void fillOp(QTreeWidgetItem *it, int i);
    QTreeWidget *tree_; QList<QToolBar *> bars_; CommandSink sink_;
    QList<QPair<class QAction *, QString>> acts_;
    bool filling_ = false;
};

// the Solids manager (mastercam.dll HistDlg tree; core_ui cui_extrude.c model: rows, images, sm_menu, sm_menu_act)
class SolidsManager : public QWidget {
    Q_OBJECT
public:
    explicit SolidsManager(QWidget *p = nullptr);
    void refresh();
    QMenu *popupMenu(int rowKind, int rowFeat, const QPoint &global);   // the right-click menu (tests: smpopup)
    std::function<void(const std::function<void()> &)> withGL;          // run with the view's GL context (bodies upload)
signals:
    void acted();                                      // a menu action ran (the main window refreshes)
private:
    QTreeWidget *tree_; bool filling_ = false;
};
class PlanesManager : public QWidget { Q_OBJECT public: explicit PlanesManager(QWidget *p = nullptr); void refresh(); protected: void resizeEvent(QResizeEvent *) override; private: QTableWidget *tab_; };
class LevelsManager : public QWidget { Q_OBJECT public: explicit LevelsManager(QWidget *p = nullptr); void refresh(); signals: void levelsChanged(); protected: void resizeEvent(QResizeEvent *) override; private: QTableWidget *tab_; };
class RecentManager : public QWidget {
    Q_OBJECT
public:
    RecentManager(CommandSink sink, QWidget *p = nullptr);
    void refresh(const QStringList &recent);
private:
    QVBoxLayout *box_; CommandSink sink_;
};
