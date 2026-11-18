// opdlg - the toolpath (operation parameter) dialog, mcmill ToolpathDlg = mccoreres dialog 6544, and the Tool List
// dialog 6410 behind the Tool page's "Select library tool...", as Qt widgets over core_ui's model
// (viewer/core_ui/cui_opdlg.c: title, page tree, Linking rows, pictures, quick view, toolbar, every page's controls =
// pp_page_body; cui_toolpage.c: the Tool page 25524 / 6556, menu 1247, the Tool List; cui_ops.c: the pending op, OK =
// generate_pending, Cancel = pp_cancel). viewer/opparams_ui.inc draws the same dialog with nuklear.
// Layout as mcamui: the frame, the Tool page and the Tool List at their template positions (dialog units -> px with the
// templates' font, chainDu), the other pages row-laid in mcamui's row order (pp_page_body's row() calls), a page picture
// at its template rectangle with the controls beside it (pp_wrap_rect).
#pragma once
#include "chaindlg.h"
#include "core_ui/cui_opfields.h"
#include <QVector>
#include <functional>

class QTreeWidget; class QTableWidget; class QLabel; class QPushButton; class QToolButton; class QVBoxLayout; class QHBoxLayout;
class QGridLayout; class QMenu;
extern bool g_regen_busy;                              // OK's computation runs (opdlg.cpp): the main window takes no commands

class OpDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit OpDialog(QWidget *parent);
    void refresh();                                    // everything from the model (mcamui draws it every frame)
    bool stale() const;                                // the model changed since the last refresh (a rebuild would drop a field being typed in)
    void dump() const;                                 // tests: the tree, the quick view and the page's widgets
    void ok();                                         // OK: unchanged -> nothing; else regenerate on a worker thread (progress / Cancel)
    void preview();                                    // Q20 Preview toolpath 10501: computed on a worker thread, the dialog stays
    void runJob(CamOp &job, const std::function<void(int, int)> &runEnd, QWidget *parent = nullptr);   // the worker thread + progress window of OK / Preview
                                                       // (and N13-9: a new 3D op's generation after its picks; PARENT = the progress window's, default this)
    void defaultsMenu();                               // Q19 (ours): the "Defaults file (ours)" button's menu
    void testInput(QWidget *prog);                     // tests: MCAMUI_OK_INPUT (a synthesized click / Esc / close to the progress window)
    bool testSet(const QString &key, const QString &v);   // tests: a control by its label (or combo#K / tp:ID / lk:ROW,WHAT) through its widget
    QMenu *toolMenu(const QPoint &global);             // menu 1247 (the list's right-click)
    std::function<void(const std::function<void()> &)> withGL;   // OK regenerates (scene uploads)
signals:
    void changed();                                    // the model changed (quick view, tree images, Tool List)
    void done();                                       // OK / Cancel ran (show_params may be off)
public:
    // the page builder (PpOps callbacks): mode 0 = build the widgets, 1 = a widget's change (control pendK_ takes pendV_)
    struct Ctl { int kind; QString label; QWidget *w; double lo, hi; };
    QVector<Ctl> ctls_;
    int mode_ = 0, k_ = 0, pendK_ = -1; double pendV_ = 0;
    QVBoxLayout *rows_ = nullptr; QGridLayout *row_ = nullptr; int rowCols_ = 1, rowH_ = 22, cell_ = 0;
    QWidget *rowBox_ = nullptr;
    void cell(QWidget *w);                             // the next cell of the current row (a new row of the same kind when full, as nuklear)
    void newRow(int h, int cols);
    void edited(int k, double v);                      // a page control changed: walk with the change, mark the page dirty, rebuild
private:
    void buildPage();
    void buildTool(QWidget *box);
    void buildLink(QWidget *box);
    void buildTemplate(QWidget *box);                  // a template page (core_ui pf_ctls) at its template positions
    // perf: the template page shown last, kept so a refresh with the same controls (same kinds / ids / rectangles /
    // live states / items) updates the values in place instead of rebuilding the page (tmplSame / tmplUpdate)
    struct TmplCtl { int kind, id, x, y, w, h, live, nd, ours, nitems, curNeg; QString pic, tip, items; QWidget *wid; };
    QVector<TmplCtl> tmpl_; QVector<QString> tmplShown_; QVector<int> tmplOn_; int tmplPg_ = -1; QWidget *tmplBox_ = nullptr;
    bool tmplSame(const PfShown *c, int n) const;
    void tmplUpdate(const PfShown *c, int n);
    void ndMark(QWidget *w, const PfShown &k);   /* UI-2 */
    QTreeWidget *tree_ = nullptr; QTableWidget *qv_ = nullptr; QLabel *status_ = nullptr; QWidget *page_ = nullptr;
    QPushButton *btn_[4] = {};
    QList<QToolButton *> tb_;
    QToolButton *dflBtn_ = nullptr;                    // Q19 (ours): Reset defaults file to original / Restore a dated backup
    bool filling_ = false, busy_ = false;
    QByteArray sig_;
    QByteArray treeSig_;                               // perf: the page tree's rows as shown (refresh skips an unchanged tree)
    QVector<int> tpCols_;                              // the Tool page list's shown columns (core_ui tp_list_cols)
};

class ToolListDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit ToolListDialog(QWidget *parent);
    void refresh();
    void dump() const;
    bool testSet(const QString &key, const QString &v);   // tests: sort=C (a header click on column C) / row=R / ok / cancel through the widgets
signals:
    void closed();                                     // OK / Cancel (tl6410_open off)
private:
    QTableWidget *list_ = nullptr; QLabel *lib_ = nullptr, *count_ = nullptr;
    QPushButton *ok_ = nullptr, *cancel_ = nullptr;
    QList<QRadioButton *> radio_;                      // Display mode 10408-10410 (greyed)
    QVector<int> rows_, cols_;                         // the shown rows (library tool index) and columns (core_ui tl6410_rows / tl6410_cols)
    bool filling_ = false;
};

class QRadioButton;
// the Planes page's dialogs (core_ui cui_planes.c): Plane Selection 842 (upl 0) / Update Plane 10269 (upl 1)
class PlanesDialog : public FloatDlg {
    Q_OBJECT
public:
    PlanesDialog(QWidget *parent, int upl);
    void refresh();
    void dump() const;
    bool testSet(const QString &key, const QString &v);   // tests: row=I / choice=K / ok / cancel through the widgets
signals:
    void closed();
private:
    int upl_; QTableWidget *list_ = nullptr; QLabel *text_ = nullptr; QRadioButton *radio_[3] = {}; QPushButton *ok_ = nullptr, *cancel_ = nullptr;
    bool filling_ = false;
};

// a CMillToolListCtrl list outside the Tool Manager (opdlg.cpp): the table, and list L's tools idx[0..n) in its column set s
QTableWidget *toolTable(QWidget *parent, const QRect &r);
void fillToolRows(QTableWidget *t, int L, const int *idx, int n, int cur, const int *cols, const int *w, int nc, const TmColSet *s);
