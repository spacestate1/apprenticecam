// acsettings - AutoCursor Settings, opmanres dialog 779 (doAutocursorDlg 0x1800db460, FT command 3340; OK =
// FUN_1800da800), laid out from its template (exports/opmanres_dialogs.tsv, dialog units -> px) and bound to core_ui's
// ac_snap / ac_ang_step (viewer/core_ui/cui_snap.c). viewer/mcamui.c draw_key_windows "show_acset".
#pragma once
#include <QDialog>

class QCheckBox; class QLineEdit;

class AcSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit AcSettingsDialog(QWidget *parent = nullptr);
protected:
    void accept() override;
private:
    QCheckBox *cb_[14] = {};                           // indexed by ACS_*
    QLineEdit *ang_ = nullptr;
};
