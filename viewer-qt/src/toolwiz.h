// toolwiz - the Define Tool / Edit Tool wizard (formats/toolmanager_ui.md section 2) and its Coolant dialog (2.12a) as Qt
// widgets over core_ui's model (viewer/core_ui/cui_toolwiz.c: tedit / tws / tw_page, the step list, Back / Next / Finish,
// the standard sizes and their warning, the Finalize Properties rows; viewer/toolwiz.c: the per-type Define rows, the edit
// box rules, ToolValidityChecker; viewer/toolprofile.c: the tool picture). The wizard is WPF in Mastercam (no dialog
// template): the window is laid out as mcamui's draw_tool_editor (a 200 px step list, an 810 px page, buttons below).
#pragma once
#include "chaindlg.h"
#include <QVector>

class QLabel; class QPushButton; class QCheckBox; class QComboBox; class QLineEdit; class QFrame; class QScrollArea;

class ToolWizardDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit ToolWizardDialog(QWidget *parent);
    void refresh(bool force = false);                  // from the model (the page rebuilt when it changed)
    void dump() const;                                 // tests: the widgets
    bool testSet(const QString &key, const QString &v);   // tests: a control through its widget
signals:
    void changed();                                    // the model changed (Finish: the part / library)
private:
    void buildPage();
    void act(const std::function<void()> &fn);
    QWidget *side_ = nullptr, *page_ = nullptr; QLabel *cap_ = nullptr;
    QPushButton *btn_[5] = {};                         // Back, Next, Finish, Cancel, Help
    QFrame *warn_ = nullptr; QCheckBox *warnDont_ = nullptr;
    struct Ctl { QString key; QWidget *w; };
    QVector<Ctl> ctls_;
    bool filling_ = false;
    QByteArray sig_;
};

class CoolantDialog : public FloatDlg {
    Q_OBJECT
public:
    explicit CoolantDialog(QWidget *parent);
    void refresh();
    void dump() const;
    bool testSet(const QString &key, const QString &v);
signals:
    void closed();
private:
    QComboBox *std_[3] = {}; QPushButton *ok_ = nullptr, *cancel_ = nullptr;
    bool filling_ = false;
};
