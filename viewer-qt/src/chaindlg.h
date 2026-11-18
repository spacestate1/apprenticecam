// chaindlg - the chaining dialogs, laid out from their decoded templates (dialog units -> px with the dialog's font, as
// acsettings.cpp does for dialog 779) and bound to core_ui's chaining model (viewer/core_ui/cui_chain.c):
//   ChainDialog      mccoreres 6464 / 1058 "Chaining" (CChainDlg, UIVertDialog with the expand / contract title icon)
//   DrillDialog      mcmillres 2300 "Drill Point Selection" (CDrillPtSelection, tp_pt_mgr)
//   ChainOptsDialog  mccoreres 405 "Chaining Options" (CChainOptions)
// viewer/mcamui.c + chaindlg_ui.inc draw the same dialogs with nuklear.
#pragma once
#include <QWidget>
#include <QDialog>
#include <QVector>
#include <QPair>
#include <functional>
extern "C" {
#include "scene.h"
}
#include "core_ui/core_ui.h"

class QLabel; class QToolButton; class QCheckBox; class QRadioButton; class QComboBox; class QLineEdit; class QAbstractButton;

// a tool window with a drawn title strip (the nuklear windows' title; the Chaining dialog's UIVertDialog icon sits in it)
class FloatDlg : public QWidget {
    Q_OBJECT
public:
    FloatDlg(const QString &title, QWidget *parent);
    QWidget *body() { return body_; }
    void setTitleIcon(const QString &icon, const QString &tip);   // empty = none
    void setBodySize(QSize s);
    int titleH() const;
    void setClosable(bool on) { closable_ = on; update(); }   // no close box (NK_WINDOW_CLOSABLE off: the op dialog)
    void setTitle(const QString &t) { title_ = t; setWindowTitle(t); update(); }
signals:
    void closeClicked();                                // the title's close box (NK_WINDOW_CLOSABLE)
    void iconClicked();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    bool event(QEvent *e) override;
private:
    QRect iconRect() const; QRect closeRect() const;
    QString title_, icon_, iconTip_;
    QWidget *body_ = nullptr;
    QPoint dragOff_; bool dragging_ = false; bool closable_ = true;
};

// one control of the Chaining template (bitmap / text button, radio, combo, check box, group box)
class ChainDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit ChainDialog(QWidget *parent);
    void rebuild();                                     // from the template now (expanded 1058 / collapsed 6464)
    void refresh();                                     // pressed / enabled states, the combo, radios, Wait
    bool expanded() const { return exp_; }
    bool testClick(int id);                             // tests: a control by id through its widget
    void dump() const;
signals:
    void act(int id);                                   // a control: core_ui chain_act(id)
private:
    struct Ctl { const ChCtl *c; QWidget *w; };
    QVector<Ctl> ctls_;
    bool exp_ = false; bool built_ = false; const ChCtl *tmpl_ = nullptr;
};

// mcgeomsldres 134 "Pick Reference Face" (SRefFaceDlg; core_ui SREF_134): Other face / OK / Help
class RefFaceDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit RefFaceDialog(QWidget *parent);
    bool testClick(int id);
signals:
    void act(int id);                                   // 273 Other face, 1 OK
private:
    QVector<QPair<int, QAbstractButton *>> b_;
};

class DrillDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit DrillDialog(QWidget *parent);
    void refresh();
signals:
    void changed();
    void ok();
private:
    QAbstractButton *un_ = nullptr, *unall_ = nullptr, *ok_ = nullptr;
};

class ChainOptsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChainOptsDialog(QWidget *parent);
    void dump() const;
protected:
    void accept() override;
    void reject() override;
private:
    ChainOpts o_;
    QVector<QPair<QCheckBox *, int *>> checks_;
    QRadioButton *cw_ = nullptr, *ccw_ = nullptr, *oneway_ = nullptr, *zig_ = nullptr;
    QComboBox *sort_ = nullptr, *sync_ = nullptr;
    QLineEdit *ang_ = nullptr, *tol_ = nullptr, *ptol_ = nullptr;
};

// dialog units -> px with this widget's font (Windows MapDialogRect: average char width / 4, height / 8)
QRect chainDu(const QWidget *w, int x, int y, int wd, int h);
