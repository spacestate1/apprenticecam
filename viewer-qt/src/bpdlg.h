// bpdlg - the classic Backplot and the Verify window in Qt, bound to core_ui's model (viewer/core_ui/cui_backplot.c,
// cui_verify.c; formats/backplot.md). viewer/mcamui.c + backplot_ui.inc draw the same model with nuklear:
//   BackplotDialog   dialog 14000 "Backplot" (14001 collapse / expand, ToolpathControl 16000: toggles 31001-31007, row 2,
//                    OK / Help, Operation List 17000 + menu 19100, info area 12000: Details 13000 / Info 4006)
//   BpVcrBar         dialog 15000, the 30 px strip at the top of the graphics view (VCR buttons, Trace / Run, run speed,
//                    position slider with the stop ticks, Conditional stops, Help)
//   BpOptionsDialog  CBackplotDlg 1228 (Options 31007) at its template positions (core_ui BPO_CTLS)
//   BpStopsDialog    BackplotDisplayStop 6000 (Conditional Stops), mcamui's rows
//   (the Verify window moved to simwin.cpp: the Mastercam Simulator window, formats/simulator.md)
#pragma once
#include "chaindlg.h"
#include <QHash>
class QTreeWidget; class QTreeWidgetItem; class QTabBar; class QLineEdit; class QCheckBox; class QLabel; class QToolButton; class QPushButton;

class BpPage : public QWidget {                        // a Details / Info page drawn from its template (core_ui BpCtl)
public:
    explicit BpPage(QWidget *parent) : QWidget(parent) {}
protected:
    void paintEvent(QPaintEvent *) override;
};

class BackplotDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit BackplotDialog(QWidget *parent);
    void refresh();                                    // the model -> the window (height, toggles, tree, page)
    void dump() const;
    bool testSet(const QString &key, const QString &v);
signals:
    void changed();
private:
    void layoutBody();
    QToolButton *exp_ = nullptr, *tgl_[7] = {}, *row2_[5] = {};
    QPushButton *ok_ = nullptr, *help_ = nullptr;
    QTreeWidget *tree_ = nullptr; QTabBar *tabs_ = nullptr; BpPage *page_ = nullptr;
    QVector<BpTreeRow> rows_; bool filling_ = false; int shownH_ = -1;
};

class BpVcrBar : public QWidget {                      // child of the graphics view, at (0, 1), view width x 30
    Q_OBJECT
public:
    explicit BpVcrBar(QWidget *view);
    int hit(QPoint p) const;                           // the button id under p (0 none)
    QRect btnRect(int k) const;
    QRect speedRect() const { return QRect(198, 2, 100, 26); }
    QRect posRect() const { return QRect(298, 2, width() - 346, 26); }
signals:
    void changed();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    bool event(QEvent *e) override;
private:
    void slide(QPoint p);
    int press_ = 0, drag_ = 0, hover_ = 0;
};

class BpOptionsDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit BpOptionsDialog(QWidget *parent);
    void refresh();
    void dump() const;
    bool testSet(const QString &key, const QString &v);
signals:
    void changed();
private:
    struct Ctl { const BpoCtl *c; QWidget *w; QLabel *swatch; QToolButton *sel; QToolButton *dn; };
    QVector<Ctl> ctls_; bool filling_ = false;
};

class BpStopsDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit BpStopsDialog(QWidget *parent);
    void refresh();
signals:
    void changed();
private:
    QCheckBox *ops_ = nullptr, *tc_ = nullptr, *step_ = nullptr, *at_[3] = {}, *ch_[3] = {};
    QLineEdit *txt_[4] = {}; bool filling_ = false;
};

void bp_widget_font(QWidget *w);                       // the dialogs' 11 px font (as chaindlg.cpp dlg_font)
QPixmap bp_bitmap(const QString &img, int w, int h);   // a backplotres / mccoreres bitmap from the icon pack (letter tile otherwise)
