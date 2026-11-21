#include "mainwindow.h"
#include "stockdlg.h"
#include "nestdlg.h"
#include "icons.h"
#include "viewwidget.h"
#include "backstage.h"
#include "geopanel.h"
#include "expanel.h"
#include "filpanel.h"
#include "acsettings.h"
#include "chaindlg.h"
#include "opdlg.h"
#include "toolmgr.h"
#include "toolwiz.h"
#include "bpdlg.h"
#include "simwin.h"
#include "dialogs8.h"
#include "lfacedlg.h"
#include "plseldlg.h"
#include "homedlg.h"
#include <QClipboard>
#include <QScreen>
#include <QTimer>
#include <QBitmap>
#include <QCursor>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QUrl>
#include "core_bridge.h"
#include "SARibbonBar.h"
#include "SARibbonCategory.h"
#include "SARibbonContextCategory.h"
#include "SARibbonPanel.h"
#include "SARibbonGallery.h"
#include "SARibbonGalleryGroup.h"
#include "SARibbonQuickAccessBar.h"
#include "SARibbonApplicationButton.h"
#include "SARibbonTabBar.h"
#include "DockManager.h"
#include "DockWidget.h"
#include "DockAreaWidget.h"
#include "DockWidgetTab.h"
#include <QAction>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDialog>
#include <QMenu>
#include <QStatusBar>
#include <QLabel>
#include <QToolButton>
#include <QStackedWidget>
#include <QFileDialog>
#include <QComboBox>
#include <QDialog>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QCheckBox>
#include <QCloseEvent>
#include <QFile>
#include <QFileInfo>
#include <QApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QKeySequence>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QGroupBox>
#include <QPushButton>
#include "cp1252.h"
#include <QAbstractSpinBox>
#include <QToolTip>
#include <QPainter>
#include <QMouseEvent>
#include <cstring>
extern "C" {
#include "scene.h"
}
#include "core_ui/core_ui.h"

/* core_ui's front-end hooks (cui_hooks): message boxes, the tree after an operation change */
static MainWindow *g_mw;
static void hook_message(const char *title, const char *text) { if (g_mw) g_mw->message(QString::fromUtf8(title), QString::fromUtf8(text)); }
static void hook_ops_changed(void) { if (g_mw) g_mw->refreshTree(); }
static void hook_dop_resume(int a) { if (g_mw) g_mw->dopResume(a); }   /* dialog 977 answered from a script step (cui_dirty.c) */
static void hook_new_part(void) { if (g_mw) g_mw->newPart(); }        /* geo_start / geoline with nothing open (mcamui do_action(ACT_NEW)) */
static void hook_geo_started(void) { if (g_mw) g_mw->geoStarted(); }  /* geo_start (mcamui: cam_mode / show_params off, dock_show_panel) */
static V3 g_probe; static bool g_probe_on;                             /* the snapat: test probe's marker (testhooks) */
void qt_snap_probe(const V3 *p) { if (p) { g_probe = *p; g_probe_on = true; } else g_probe_on = false; }
static void hook_clear_ops(void) { cam_mode = 0; show_params = 0; tool_choice = 0; cam_clear_selection(); }   /* doc_clear_ops (mcamui hook_clear_ops: the chaining / op dialog end) */
/* core_ui cui_ops.c cm_run: the Toolpaths manager menu items only a front end has. Expand / Collapse = the tree; Tool list...
   (RunToolReportForSelectedOps: core_ui tool_report) and Renumber tools... (CRenumbTl 805: core_ui rn_open) since phase 7;
   Edit Common Parameters (243), Rename (the input box) are phase 8 here (greyed: menuHas) - mcamui hook_op_menu */
static void hook_op_menu(int act) {
    if (g_mw && (act == CM_EXPAND || act == CM_COLLAPSE)) g_mw->treeExpand(act == CM_EXPAND);
    else if (act == CM_TOOLLIST) tool_report();
    else if (act == CM_RENUMTOOLS) rn_open();
    else if (act == CM_EDIT) ecp_open();                 /* RunEditCommonParams: CEditCommonDlg on the selected ops (core_ui cui_ecp.c, phase 8) */
    else if (act == CM_GRP_RENAME) grp_rename_begin();   /* 3585: TVM_EDITLABEL on the clicked group (core_ui cui_misc.c; the tree edit: syncMisc) */
}
/* the Tool page's hooks (core_ui cui_toolpage.c, phase 7): the Define / Edit Tool wizard on a part tool (-1 new), the Tool
   Manager, a library opened (the library list's selection resets), the Tool Manager closed */
static void hook_tool_manager(void) { show_tm = 1; }
/* KeyPress(VK) (mastercam.dll @1803221b0, decomp/getevent/mastercam_keypress.c 7-28; core_ui cui_hooks.key_press): asked between
   steps of a computation on the UI thread (Geometry Nesting's TrueShape run). Windows keeps the user's input in the thread's queue
   while the run goes on; each KeyPress call takes ONE keyboard message out of it (PeekMessage(0x100..0x109, PM_REMOVE)):
   - a key-down (WM_KEYDOWN / WM_SYSKEYDOWN) of VK -> 1 (the run stops);
   - another key-down -> FUN_1803222f0 (decomp/getevent/mastercam_keypress.c 31-124 = decomp/mastercam_keyrot2.c, ported as
     ViewInput::viewKey): the arrow / Alt+arrow / Page Up / Page Down / Home view keys, only while the graphics window has the focus;
   - any other keyboard message (key-up, characters) is removed and nothing done.
   Everything else (mouse, wheel, close, ...) stays queued and is handled after the run, as Windows dispatches it then.
   Here: the window system's pending events are delivered through a filter that keeps every user input event in arrival order;
   a call takes the first keyboard event of that queue as above; at the run's end (cui_hooks.key_press_end) the queue left is
   posted again in order. The main window takes no command meanwhile (g_regen_busy). Tests: nestescat:N posts an Esc key-down
   before the N-th call (testhooks.cpp) */
int g_key_press_esc_at, g_key_press_calls;
namespace {
struct HeldEvent { QPointer<QObject> to; QEvent *e; bool key; bool view; };
struct KeyProbe : public QObject {
    QList<HeldEvent> held;
    bool eventFilter(QObject *o, QEvent *e) override {
        switch (e->type()) {
        case QEvent::ShortcutOverride: e->accept(); return true;   /* no shortcut takes the key: it is held as a key event below */
        case QEvent::KeyPress: case QEvent::KeyRelease: {
            QWidget *f = QApplication::focusWidget(), *vw = g_mw ? g_mw->view()->widget() : nullptr;
            held.append({ o, e->clone(), true, f && vw && (f == vw || vw->isAncestorOf(f)) }); return true; }
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease: case QEvent::MouseButtonDblClick: case QEvent::MouseMove:
        case QEvent::Wheel: case QEvent::Shortcut:
        case QEvent::TouchBegin: case QEvent::TouchUpdate: case QEvent::TouchEnd: case QEvent::TabletPress: case QEvent::ContextMenu:
        case QEvent::DragEnter: case QEvent::DragMove: case QEvent::Drop: case QEvent::Close:
            held.append({ o, e->clone(), false, false });
            if (e->type() == QEvent::Close) e->ignore();
            return true;
        default: return false;
        }
    }
};
KeyProbe g_key_probe;
}
static int hook_key_press(int vk) {
    g_key_press_calls++;
    if (g_key_press_esc_at > 0 && g_key_press_calls == g_key_press_esc_at && g_mw) {   /* the test's Esc (nestescat:N) */
        QWidget *t = QApplication::focusWidget() ? QApplication::focusWidget() : (QWidget *)g_mw;
        QCoreApplication::postEvent(t, new QKeyEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier));
    }
    bool busy0 = g_regen_busy; g_regen_busy = true;
    qApp->installEventFilter(&g_key_probe);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    qApp->removeEventFilter(&g_key_probe);
    g_regen_busy = busy0;
    for (int i = 0; i < g_key_probe.held.size(); i++) {           /* PeekMessage(0x100..0x109, PM_REMOVE): the first keyboard message */
        if (!g_key_probe.held[i].key) continue;
        HeldEvent h = g_key_probe.held.takeAt(i);
        int r = 0;
        if (h.e->type() == QEvent::KeyPress) {                     /* (message - 0x100) & ~4 == 0: WM_KEYDOWN / WM_SYSKEYDOWN */
            auto *k = static_cast<QKeyEvent *>(h.e);
            if (vk == 0x1b && k->key() == Qt::Key_Escape) r = 1;
            else if (h.view && g_mw) if (ViewInput *in = viewInput(g_mw->view())) in->viewKey(k->key(), k->modifiers());   /* FUN_1803222f0 */
        }
        delete h.e;
        return r;
    }
    return 0;
}
static void hook_key_press_end(void) {                             /* the run ended: the input left in the queue is handled now */
    QList<HeldEvent> q; q.swap(g_key_probe.held);
    for (HeldEvent &h : q) { if (h.to) QCoreApplication::postEvent(h.to, h.e); else delete h.e; }
}
static void hook_tm_closed(void) { }
/* the system units changed (core_ui cui_units_set): the current configuration's items read again, then the graphics / dialogs
   (ChangeConfiguration -> ReadCfgFile; FUN_180170520 -> RefreshStatusBar) */
static void hook_config_reload(void) { qc_reload_config(); if (g_mw) { g_mw->syncMisc(); g_mw->view()->redraw(); } }
static void hook_view_port(void) { if (g_mw) if (ViewInput *in = viewInput(g_mw->view())) in->setViewport(); }   /* mcamui vp_main */
static void hook_chain_ok(void) { if (g_mw) g_mw->chainOk(); }
static void hook_chain_ask(void) { if (g_mw) g_mw->chainAskUnselectAll(); }
static void hook_selrun(int kind) { if (g_mw) g_mw->openSelrun(kind); }
/* a core Yes / No question (core_ui cui_hooks.ask_yes_no; MCMessageBox MB_YESNO | MB_ICONQUESTION, Yes the default button);
   a script step answers with cui_script_yes (askyes:0 / askyes:1) */
extern bool g_qt_scripting;
static int hook_ask_yes_no(const char *cap, const char *text) {
    if (g_qt_scripting || !g_mw) { printf("    (asked \"%s\": %s)\n", cap, cui_script_yes ? "Yes" : "No"); return cui_script_yes; }
    return QMessageBox::question(g_mw, QString::fromUtf8(cap), QString::fromUtf8(text), QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
}
/* a message box with a check box (core_ui cui_hooks.message_check): L-14, mclathe CToolModWarning dialog 3275 (static 503 + check 500
   "Turn off warning for session..." + OK); a script answers with cui_script_check */
static int hook_message_check(const char *title, const char *text, const char *check) {
    if (g_qt_scripting || !g_mw) { printf("    MESSAGE: %s [%s: %d]\n", text, check, cui_script_check); return cui_script_check; }
    QMessageBox mb(QMessageBox::NoIcon, QString::fromUtf8(title), QString::fromUtf8(text), QMessageBox::Ok, g_mw);
    QCheckBox *cb = new QCheckBox(QString::fromUtf8(check));
    mb.setCheckBox(cb);
    mb.exec();
    return cb->isChecked();
}
/* N1: the NC file's Save As (PromptForFileToSave: initial folder / name = the NC name, filter "<suffix> Files (*.<suffix>)" + mccoreres
   2858 "All Files (*.*)", default extension the suffix, OFN_OVERWRITEPROMPT - QFileDialog asks before replacing a file); a script
   accepts the name (cui_script_yes) */
static int hook_ask_save_path(char *path, size_t n, const char *suffix) {
    QString sfx = QString::fromUtf8(suffix && suffix[0] == '.' ? suffix + 1 : (suffix ? suffix : "nc"));
    if (g_qt_scripting || !g_mw) { printf("    (save as \"%s\": %s)\n", path, cui_script_yes ? "OK" : "Cancel"); return cui_script_yes; }
    QString filt = QString("%1 Files (*.%2);;All Files (*.*)").arg(sfx.toUpper(), sfx);   /* the label's case: NOT DECODED */
    QFileDialog dlg(g_mw, QString(), QString::fromUtf8(path), filt);
    dlg.setAcceptMode(QFileDialog::AcceptSave); dlg.setDefaultSuffix(sfx);
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return 0;
    snprintf(path, n, "%s", dlg.selectedFiles().first().toUtf8().constData());
    return 1;
}
/* Geometry Nesting's file dialogs (core_ui cui_hooks.ask_file, NESTING.DLL FUN_1800d6590 -> PromptForFileToOpen / ToSave); scripts:
   Cancel (the step nestfile:PATH answers in the core) */
static int hook_ask_file(char *path, size_t n, int save, const char *title, const char *filter, const char *suffix) {
    if (g_qt_scripting || !g_mw) { printf("    (file dialog \"%s\": Cancel)\n", title); return 0; }
    QFileDialog dlg(g_mw, QString::fromUtf8(title), QString::fromUtf8(path), QString::fromUtf8(filter) + ";;All Files (*.*)");
    dlg.setAcceptMode(save ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    if (!save) dlg.setFileMode(QFileDialog::ExistingFile);
    if (suffix && suffix[0]) dlg.setDefaultSuffix(QString::fromUtf8(suffix[0] == '.' ? suffix + 1 : suffix));
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return 0;
    snprintf(path, n, "%s", dlg.selectedFiles().first().toUtf8().constData());
    return 1;
}
/* a dialog of push buttons (core_ui cui_hooks.ask_choice: e.g. mccoreres 6561 "Deleting a referenced entity!"); the last button is
   Cancel (also the Esc / close answer); '&' marks the mnemonic as in the template */
static int hook_ask_choice(const char *cap, const char *text, const char **buttons, int n) {
    if (g_qt_scripting || !g_mw) { printf("    (dialog \"%s\": %s)\n", cap, cui_script_choice >= 0 && cui_script_choice < n ? buttons[cui_script_choice] : "?"); return cui_script_choice; }
    QMessageBox mb(QMessageBox::NoIcon, QString::fromUtf8(cap), QString::fromUtf8(text), QMessageBox::NoButton, g_mw);
    QList<QAbstractButton *> bs;
    for (int i = 0; i < n; i++) bs << mb.addButton(QString::fromUtf8(buttons[i]), i == n - 1 ? QMessageBox::RejectRole : QMessageBox::AcceptRole);
    mb.exec();
    int r = bs.indexOf(mb.clickedButton());
    return r < 0 ? n - 1 : r;
}
/* get_angle / get_real's modal input (core_ui cui_hooks.ask_real; mastercam.dll RequestRealInputModal @180327430 -> opmanres dialog 286:
   a frameless-looking 154 x 16 du box whose title is the prompt, one editable ComboBox 254; DoModal != IDCANCEL -> EvalToDouble of the
   text). Ours: the text is read as a number (Mastercam's expression evaluator EvalToDouble is NOT PORTED: a non-number = Cancel); the
   combo's history list NOT DECODED (empty). A script answers with cui_script_real (step dimreal:V | dimreal:cancel) */
static int hook_ask_real(const char *prompt, double *v) {
    if (g_qt_scripting || !g_mw) { printf("    (asked \"%s\": %s)\n", prompt, cui_script_real_ok ? QByteArray::number(cui_script_real, 'g', 12).constData() : "Cancel");
        if (cui_script_real_ok) *v = cui_script_real; return cui_script_real_ok; }
    QDialog dlg(g_mw); dlg.setWindowTitle(QString::fromUtf8(prompt));
    QComboBox *cb = new QComboBox(&dlg); cb->setEditable(true); cb->setEditText(QString::number(*v, 'g', 12)); cb->setMinimumWidth(231);
    QVBoxLayout *lay = new QVBoxLayout(&dlg); lay->setContentsMargins(2, 3, 2, 3); lay->addWidget(cb);
    QObject::connect(cb->lineEdit(), &QLineEdit::returnPressed, &dlg, &QDialog::accept);
    if (dlg.exec() != QDialog::Accepted) return 0;
    bool ok = false; double x = cb->currentText().trimmed().toDouble(&ok);
    if (!ok) return 0;
    *v = x; return 1;
}
/* Drafting > Note's dialog (core_ui cui_hooks.note_dialog): mccoreres 452 "Note Dialog" at its template positions (core_ui NOTE_452, chainDu's
   dialog units), CNoteDlg's behaviour (core_ui dn_dlg_enabled: OnInitDialog FUN_1805bfc00, the radio / edit handlers FUN_1805bf9c0 /
   FUN_1805bf950 / FUN_1805bfb60): the multi-line edit 25300 in the note font if TrueType, else Arial, at 1.5 x the dialog font height
   (OnInitDialog l.257-279: the stroke fonts are not TrueType, so Arial); the controls core_ui does not port are greyed (dn_dlg_ported).
   The edit's line breaks are "\r\n" (the Windows edit control's; the note text keeps them). A script answers with the notedlg step */
QRect chainDu(const QWidget *, int x, int y, int wd, int h);
/* S4: mcmsurfres dialog 100 "Select Boss/Cavity" (CBossCavity, decomp/mcmsurf_all.c FUN_180024f00 / FUN_1800250b0) at its template
   positions (exports/mcmsurfres_dialogs.tsv: 127 x 90 du; group box 10,0 65 x 55; radios 512 / 514 / 516 at 15,10 / 15,25 / 15,40; OK
   44,70 and Cancel 84,70, 36 x 18): DDX_Radio 512 writes *SEL at OK only (core_ui cui_chain.c ms_begin). A script answers with
   cui_script_radio (step bosscav:N) */
static void hook_gen_queued() { if (g_mw) g_mw->queueGen(); }   /* N13-9: core_ui cui_chain.c ms_generate */
static int hook_radio_dialog(int dlg, const char *title, const char **labels, int n, int *sel) {
    if (g_qt_scripting || !g_mw) {
        int ok = cui_script_radio >= 0 && cui_script_radio < n; if (ok) *sel = cui_script_radio;
        printf("    (dialog \"%s\": %s)\n", title, ok ? labels[*sel] : "Cancel"); return ok;
    }
    (void)dlg;
    QDialog d(g_mw); d.setWindowTitle(QString::fromUtf8(title)); d.setFixedSize(chainDu(&d, 0, 0, 127, 90).size());
    auto *g = new QGroupBox(QString(), &d); g->setGeometry(chainDu(&d, 10, 0, 65, 55));
    static const int Y[3] = { 10, 25, 40 }, W[3] = { 31, 35, 49 };
    QRadioButton *rb[3] = { nullptr };
    for (int i = 0; i < n && i < 3; i++) { rb[i] = new QRadioButton(QString::fromUtf8(labels[i]), &d); rb[i]->setGeometry(chainDu(&d, 15, Y[i], W[i] + 20, 10)); rb[i]->setChecked(i == *sel); }
    auto *ok = new QPushButton("&OK", &d); ok->setGeometry(chainDu(&d, 44, 70, 36, 18)); ok->setDefault(true);
    auto *cn = new QPushButton("&Cancel", &d); cn->setGeometry(chainDu(&d, 84, 70, 36, 18));
    QObject::connect(ok, &QPushButton::clicked, &d, &QDialog::accept); QObject::connect(cn, &QPushButton::clicked, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return 0;
    for (int i = 0; i < n && i < 3; i++) if (rb[i]->isChecked()) *sel = i;
    return 1;
}
static int hook_note_dialog(char *text, size_t n, int *radio) {
    if (g_qt_scripting || !g_mw) {
        printf("    (note dialog: %s)\n", cui_script_note_ok ? QByteArray::number(cui_script_note_radio).constData() : "Cancel");
        if (!cui_script_note_ok) return 0;
        snprintf(text, n, "%s", cui_script_note_text); *radio = cui_script_note_radio; return 1;
    }
    QDialog dlg(g_mw); dlg.setWindowTitle("Note Dialog"); dlg.setFixedSize(chainDu(&dlg, 0, 0, 288, 174).size());
    QPlainTextEdit *ed = nullptr; QRadioButton *rb[8] = { nullptr }; QPushButton *ok = nullptr; QList<QPair<unsigned, QWidget *>> ctl;
    for (const BsCtl *k = NOTE_452; k->text; k++) {
        QRect r = chainDu(&dlg, k->x, k->y, k->w, k->h); QWidget *w = nullptr; QString t = QString::fromUtf8(k->text);   /* '&' = the mnemonic, as Windows */
        if (k->cls == 2) {
            ed = new QPlainTextEdit(&dlg); QFont f("Arial"); f.setPointSizeF(dlg.font().pointSizeF() * 1.5); ed->setFont(f);
            ed->setPlainText(Cp1252::toQ(text).replace("\r\n", "\n")); w = ed;   /* the text is Windows-1252 (records.md 0) */
        } else if (k->cls == 8) { auto *g = new QGroupBox(t, &dlg); g->lower(); w = g; }
        else if (k->cls == 5) { int i = dn_dlg_radio_index(k->id); rb[i] = new QRadioButton(t, &dlg); rb[i]->setChecked(i == *radio); w = rb[i]; }
        else { auto *b = new QPushButton(t, &dlg); w = b;
            if (k->id == 1) { ok = b; b->setDefault(true); QObject::connect(b, &QPushButton::clicked, &dlg, &QDialog::accept); }
            if (k->id == 2) QObject::connect(b, &QPushButton::clicked, &dlg, &QDialog::reject); }
        w->setGeometry(r); ctl << qMakePair(k->id, w);
    }
    auto refresh = [&] {
        int ri = 0; for (int i = 0; i < 8; i++) if (rb[i] && rb[i]->isChecked()) ri = i;
        bool empty = ed->toPlainText().isEmpty();
        for (auto &c : ctl) if (c.first) c.second->setEnabled(dn_dlg_ported(c.first) && dn_dlg_enabled(c.first, ri, empty));
    };
    for (int i = 0; i < 8; i++) if (rb[i]) QObject::connect(rb[i], &QRadioButton::toggled, &dlg, refresh);
    QObject::connect(ed, &QPlainTextEdit::textChanged, &dlg, refresh);
    refresh(); (void)ok;
    if (dlg.exec() != QDialog::Accepted) return 0;
    for (int i = 0; i < 8; i++) if (rb[i] && rb[i]->isChecked()) *radio = i;
    QByteArray b = Cp1252::fromQ(ed->toPlainText().replace("\n", "\r\n"));
    snprintf(text, n, "%s", b.constData());
    return 1;
}
static void hook_solids_cleared(void) { ex_mode = 0; }                 /* solids_clear: the Extrude command ends (mcamui hook_solids_cleared) */
static int hook_msg_open(const char **t, const char **x) { return g_mw && g_mw->msgShown(t, x); }   /* core_ui cui_extrude_script */
static void hook_msg_close(void) { if (g_mw) g_mw->msgClose(); }
bool g_qt_scripting;                                                   /* testhooks: a MCAMUI_SCRIPT step runs (mcamui runs the whole script before its first frame) */
static void hook_ov_extra(float **pv, int *pn, int *pcap) {
    chain_ov_holes(pv, pn, pcap);                                       /* core_ui: the picked drill points (mcamui ov_extra order) */
    geo_ov_extra(pv, pn, pcap);                                         /* core_ui: drawing preview, rubber band, snap marker */
    ex_ov_arrows(pv, pn, pcap);                                         /* core_ui: the Extrude drag arrows (mcamui ov_extra, after the stock box) */
    fl_ov(pv, pn, pcap);                                                /* core_ui: the Constant Fillet's picked bodies / faces / edges (cui_fillet.c) */
    if (g_probe_on) ov_fast_marker(pv, pn, pcap, g_probe);
}

static bool has(const QString &hay, const char *needle) { return hay.contains(QLatin1String(needle), Qt::CaseInsensitive); }   /* case-insensitive substring */
static bool starts(const QString &t, const char *p) { return t.startsWith(QLatin1String(p), Qt::CaseInsensitive); }
static bool same(const QString &t, const char *p) { return t.compare(QLatin1String(p), Qt::CaseInsensitive) == 0; }

/* the context-tab colours of mcamui.c draw_tabs (ours: the BCGP context colours of the ribbon XML are NOT DECODED):
   a 3 px strip rgb(66,165,82) over the tab, text rgb(32,120,50), the tab itself in the active-tab fill rgb(248,249,250) */
static const QColor CONTEXT_GREEN(66, 165, 82), CONTEXT_TEXT(32, 120, 50), CONTEXT_FILL(248, 249, 250);

MainWindow::MainWindow(UiLayout &ui, QWidget *parent) : SARibbonMainWindow(parent), ui_(ui) {
    g_mw = this;
    cui_hooks.message = hook_message; cui_hooks.ops_changed = hook_ops_changed;
    cui_hooks.new_part = hook_new_part; cui_hooks.geo_started = hook_geo_started;   /* core_ui cui_geo.c (phase 3) */
    cui_hooks.ov_picked = geo_ov_picked; cui_hooks.ov_extra = hook_ov_extra;
    cui_hooks.ov_chained = ov_chained; cui_hooks.clear_ops = hook_clear_ops;       /* core_ui cui_chain.c (phase 4) */
    cui_hooks.view_port = hook_view_port; cui_hooks.chain_ok = hook_chain_ok; cui_hooks.chain_ask_unselect_all = hook_chain_ask;
    cui_hooks.selrun_dialog = hook_selrun;                /* Home > Change Level / Set All (core_ui cui_attr.c) */
    cui_hooks.ask_yes_no = hook_ask_yes_no; cui_hooks.message_check = hook_message_check; cui_hooks.ask_choice = hook_ask_choice; cui_hooks.ask_save_path = hook_ask_save_path; cui_hooks.ask_file = hook_ask_file;
    cui_hooks.key_press = hook_key_press; cui_hooks.key_press_end = hook_key_press_end;
    cui_hooks.ask_real = hook_ask_real;                 /* get_angle / get_real (cui_draft_dim.c) */
    cui_hooks.note_dialog = hook_note_dialog;           /* Drafting > Note (cui_draft_note.c) */
    cui_hooks.radio_dialog = hook_radio_dialog;         /* S4: dialog 100 Select Boss/Cavity (cui_chain.c) */
    cui_hooks.gen_queued = hook_gen_queued;             /* N13-9: the generation after a new 3D op's picks on the worker thread (cui_chain.c) */
    cui_hooks.solids_cleared = hook_solids_cleared; cui_hooks.msg_open = hook_msg_open; cui_hooks.msg_close = hook_msg_close;   /* core_ui cui_extrude.c (phase 5) */
    cui_hooks.op_menu = hook_op_menu;                    /* core_ui cui_ops.c (phase 6) */
    cui_hooks.ask_dwarn = [](double dm) { extern int engraveDepthWarning(QWidget *, double); return engraveDepthWarning(g_mw, dm); };   /* Engrave 1808 (opdlg.cpp; E-DW1) */
    cui_hooks.dop_resume = hook_dop_resume;              /* core_ui cui_dirty.c: dialog 977's held command */
    cui_hooks.config_reload = hook_config_reload;        /* core_ui cui_units_set (formats/units.md) */
    cui_hooks.tool_wizard = tw_open_part; cui_hooks.tool_manager = hook_tool_manager;   /* phase 7: menu 1247 / the op dialog's toolbar enable themselves */
    cui_hooks.lib_opened = tm_lib_opened; cui_hooks.tm_closed = hook_tm_closed;
    setAcceptDrops(true);                                /* a dropped part / project opens, a .tooldb / .tools opens the Tool Manager (mcamui SDL_DROPFILE) */
    cui_kmap_load();                                   /* CONFIG/mastercam.kmp, else Mastercam 2017's defaults */
    rmb_load();                                        /* CONFIG/default.mtb: the graphics right-click menu */
    qApp->installEventFilter(this);                    /* the keymap (mcamui.c kmap_dispatch before the view keys) */
    setRibbonTheme(SARibbonTheme::RibbonThemeOffice2013);
    /* greyed (not ported) commands: text rgb(160,160,160) (mcamui.c rib_button, rib_grey); the theme's qss sets one
       colour for every state */
    setStyleSheet(styleSheet() + "\nSARibbonToolButton:disabled { color: rgb(160,160,160); }\n");
    stack_ = new QStackedWidget(this);
    setCentralWidget(stack_);
    buildDocks();
    backstage_ = new Backstage(stack_);
    stack_->addWidget(backstage_);
    connect(backstage_, &Backstage::back, this, [this] { showBackstage(false); });
    /* the backstage File commands as mcamui's: New / Open through the unsaved-changes prompt, Save / Save As = the project */
    connect(backstage_, &Backstage::newPart, this, [this] { showBackstage(false); request(ACT_NEW); });
    connect(backstage_, &Backstage::openDialog, this, [this] { showBackstage(false); request(ACT_OPEN); });
    connect(backstage_, &Backstage::openPath, this, [this](const QString &p) { showBackstage(false); request(ACT_OPEN_PATH, p); });
    connect(backstage_, &Backstage::save, this, [this] { showBackstage(false); docSave(false); });
    connect(backstage_, &Backstage::saveAs, this, [this] { showBackstage(false); docSave(true); });
    connect(backstage_, &Backstage::about, this, [this] { showBackstage(false); show_about = 1; syncMisc(); });   /* Help > About (OnAppAbout) */
    /* File > Configuration: mccoreres 282 "System Configuration"; only its "Current:" combo (2056) is here - the configuration files
       with their units (4962 "(English)" / 4963 "(Metric)"); the tree / pages and Open / Save / Merge are NOT PORTED. OK with the
       other file = FUN_180170520 (core_ui cui_units_change: the scale question, the units, the configuration read again) */
    connect(backstage_, &Backstage::configuration, this, [this] {
        showBackstage(false);
        QDialog d(this); d.setWindowTitle("System Configuration");
        auto *lay = new QHBoxLayout(&d); lay->addWidget(new QLabel("Current:", &d));
        auto *cb = new QComboBox(&d); cb->addItem(QString::fromUtf8(cui_units_config(0))); cb->addItem(QString::fromUtf8(cui_units_config(1)));
        cb->setCurrentIndex(tp_metric() ? 1 : 0); cb->setMinimumWidth(240); lay->addWidget(cb);
        auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d); lay->addWidget(bb);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() == QDialog::Accepted && cui_units_change(cb->currentIndex() == 1)) { syncMisc(); view()->redraw(); }
    });
    buildRibbon();
    buildQat();
    buildStatus();
    updateTitle();
}

/* ------------------------------------------------------------------------------------------------ commands */
/* commands this recreation can carry out itself; returns 1 if handled. probe = 1: only tells whether the command is
   available here (the ribbon / menus draw the others greyed and do nothing on a click). mcamui.c run_local_x, in its
   order; the entries whose behaviour has not moved to the Qt UI yet are listed as NOT PORTED (they stay greyed). */
bool MainWindow::localRun(const QString &t, bool probe) {
#define RL(x) do { if (!probe) { x; view_->redraw(); } return true; } while (0)
    if (t.isEmpty()) return false;
    if (same(t, "Fit") || has(t, "Fit Screen")) RL(view_fit(1));
    if (starts(t, "Top")) RL(view_std_animated(1));
    if (starts(t, "Front")) RL(view_std_animated(2));
    if (starts(t, "Right")) RL(view_std_animated(5));
    if (starts(t, "Isometric")) RL(view_std_animated(7));
    if (same(t, "Shaded")) RL(scene_shade(3); updateStatusShade());         /* View tab 53771 OnShadeShaded (S-15: render_cfg 3) */
    if (same(t, "Outline Shaded")) RL(scene_shade(4); updateStatusShade()); /* View tab 53769 / 53770 OnShadeShadedOutline (render_cfg 4) */
    if (same(t, "Wireframe")) RL(scene_shade(1); updateStatusShade());      /* View tab 53765 / 53766 OnShadeWire */
    if (has(t, "Translucen")) RL(scene_translucent_toggle(); updateStatusShade());
    if (same(t, "Toolpaths") || has(t, "Toolpaths Manager")) RL(showManager(0));
    if (same(t, "Solids") || has(t, "Solids Manager")) RL(showManager(1));
    if (same(t, "Planes") || has(t, "Planes Manager")) RL(showManager(2));
    if (same(t, "Levels") || has(t, "Levels Manager")) RL(showManager(3));
    if (has(t, "Recent Functions")) RL(showManager(4));
    if (same(t, "Show Grid") || same(t, "Grid")) RL(show_grid = !show_grid);
    if (same(t, "Snap to Grid")) RL(grid.snap = !grid.snap);
    if (has(t, "Grid Settings") || has(t, "Screen Grid")) RL(show_gridset = 1; syncMisc());   /* mccoreres 1589 (core_ui cui_misc.c) */
    if (same(t, "Mill") || same(t, "Lathe") || same(t, "Router") || same(t, "Wire") || same(t, "Mill-Turn"))
        RL(machine_ = t; updateContexts());
    if (same(t, "Select all operations")) RL(sel = -1; tpm_->refresh());
    /* toolbar 800 dirty operations (core_ui cui_dirty.c): OMselectDirtyOperations / OMregenSelectedOperations / OMregenDirtyOperations,
       always enabled (COpMgrToolBar::OnUpdateOMToolbarButtons enables every button while the cursor is over the bar) */
    if (has(t, "Select all dirty operations")) RL(op_select_dirty(); tpm_->refresh());
    if (has(t, "Regenerate all selected operations")) RL(view_->withGL([] { op_regen_selected(); }); refreshAll());
    if (has(t, "Regenerate all dirty operations")) RL(view_->withGL([] { op_regen_dirty(); }); refreshAll());
    if (has(t, "Backplot selected")) RL(view_->withGL([] { bp_start(); if (bp_on) bp_frame_still(); }); syncMisc());   /* OMbackplotSelectedOperations -> classic Backplot (core_ui); a dirty op: dialog 977 first */
    if (has(t, "Verify selected")) RL(view_->withGL([] { vf_start(); if (vf_on) vf_frame_still(); }); updateStatusShade(); syncMisc());   /* OMverifySelectedOperations -> the stock-removal simulation (verify.c, ours) */
    if (has(t, "Toggle display")) RL(show_tp = !show_tp);
    if (has(t, "Hide rapid")) RL(show_rapid = !show_rapid);
    /* the File commands match by label; View > "Save" (OnGViewSave "Save Graphics View") and "New" (NewWorksheet
       "Create New Viewsheet") share the labels but their behaviour is NOT DECODED: only commands without an FT function
       (the backstage entries) or with an OnFile* one count (mcamui.c rl_file_cmd, formats/ui_commands.tsv) */
    bool fileCmd = !rlFtSet_ || rlFt_.isEmpty() || rlFt_.startsWith("OnFile", Qt::CaseInsensitive);
    if (fileCmd && same(t, "Exit")) RL(request(ACT_EXIT));
    if (fileCmd && same(t, "New")) RL(request(ACT_NEW));
    if (fileCmd && (same(t, "Open") || same(t, "Open..."))) RL(request(ACT_OPEN));
    if (fileCmd && (same(t, "Save") || same(t, "Save..."))) RL(docSave(false));      /* the .mcamui project (mcamui.c doc_save) */
    if (fileCmd && (same(t, "Save As") || same(t, "Save As..."))) RL(docSave(true));
    if (fileCmd && same(t, "Close")) RL(request(ACT_CLOSE));
    if (has(t, "MCAM part")) RL(saveDialog());        /* Save as a Mastercam part (mcamui.c export_mcam_dialog) */
    /* the wireframe commands (core_ui cui_geo.c geo_find / geo_start; mcamui.c run_local_x): a command of another module
       with a wireframe name (Toolpaths > Trim) is not one (rl_nogeo) */
    if (same(t, "Clear Colors") && (!rlFtSet_ || rlFt_ == "ClearColors")) { if (!probe) mcFunction("ClearColors", false); return true; }   /* Home > Clear Colors (FT ClearColors) */
    /* the Drafting tab (core_ui cui_draft.c): Horizontal / Vertical / Parallel by their FT functions (doDraft_DIMENSION_*): the labels
       are also the Ordinate group's (doDraft_DIMENSION_ORD_*) and the Wireframe tab's; every other doDraft_ command is not ported */
    if (rlFtSet_ && rlFt_.startsWith("doDraft_")) {
        QByteArray ft = rlFt_.toUtf8();
        if (!dr_find_ft(ft.constData()) && !do_find_ft(ft.constData())) return false;   /* + the Ordinate group (cui_draft_ord.c) */
        if (probe) return true;
        bool ok = false; view_->withGL([&] { ok = geo_start(ft.constData()) != 0; });
        if (ok) { syncGeo(); redrawView(); }
        return true;
    }
    /* the Surfaces tab by its FT functions (core_ui cui_surf.c): a Surfaces command is never a wireframe one with the same label
       (Surfaces > Offset = OffsetSurfaces, not the wireframe Offset); the ones not ported are greyed */
    if (rlFtSet_ && srf_is_ft(rlFt_.toUtf8().constData())) {
        QByteArray ft = rlFt_.toUtf8();
        if (!srf_find_ft(ft.constData())) return false;
        if (probe) return true;
        view_->withGL([&] { if (geo_cmd) geo_cancel(); srf_start(ft.constData()); });
        refreshAll(); return true;
    }
    /* Transform > Geometry Nesting (FT do_nest_g, MCCORE.DLL; button 53673): CO do_nest_g -> NE m_nest_g (core_ui cui_nest.c);
       nesting_enabled(0, 0) (CO @1805be860) is 1 unless an operation is in process */
    if (rlFtSet_ && rlFt_ == "do_nest_g") RL(view_->withGL([] { ns_start(); }); syncMisc(); syncChain(); refreshAll());
    if (rlNoGeo_) ;
    /* the Transform commands (core_ui cui_xform.c) only from their FT functions: Art > Translate / Scale (OnTranslate /
       OnScale, no dll field) share the labels */
    else if (rlFtSet_ && !rlFt_.isEmpty() && xf_cmd(geo_find(t.toUtf8().constData())) && !rlFt_.startsWith("do_xform") && rlFt_ != "DynamicXform" && rlFt_ != "TransformDistribute") ;   /* Transform > Dynamic / Distribute: FT DynamicXform / TransformDistribute */
    else if (probe) { if (geo_find(t.toUtf8().constData())) return true; }
    else { bool ok = false; QByteArray n = t.toUtf8(); view_->withGL([&] { ok = geo_start(n.constData()) != 0; }); if (ok) { syncGeo(); redrawView(); return true; } }
    /* Solids > Extrude = FT sld_newExtrude; Draft Extrude (sld_newDraftFace_3) / Surfaces > Extrude (ExtrudeSurface) share
       the "Extrude" tooltip but are other commands (greyed) - phase 5, both apps */
    if (same(t, "Extrude") && (!rlFtSet_ || rlFt_.isEmpty() || rlFt_ == "sld_newExtrude")) { if (!probe) { view_->withGL([] { ex_start(); }); refreshAll(); } return true; }   /* core_ui ex_start (mcamui run_local_x) */
    /* Solids > Modify > Constant Fillet (53594 / 53595) = FT sld_do_fillet (formats/solid_fillet.md 0; core_ui cui_fillet.c) */
    if ((rlFtSet_ && rlFt_ == "sld_do_fillet") || (same(t, "Constant Fillet") && (!rlFtSet_ || rlFt_.isEmpty()))) { if (!probe) { view_->withGL([] { fl_start(); }); refreshAll(); } return true; }
    if (same(t, "Delete Entities") || same(t, "Delete")) RL(view_->withGL([] { delete_selected_ents(); }));   /* core_ui */
    /* Post selected operations (toolbar 800, OMpostSelectedOperations): the LinuxCNC post of every operation (core_ui post_ops, as mcamui) */
    if (has(t, "Post selected")) RL(post_ops(); refreshAll());   /* a dirty op: dialog 977 first (cui_dirty.c dop_check) */
    /* the Tool Manager (RunMillToolManager / RunLatheToolManager: "Tool Manager", "Mill Tool Manager", "Lathe Tool Manager" -
       mcamui.c run_local_x matches the label; core_ui cui_toolmgr.c, phase 7) */
    if (has(t, "Tool Manager")) RL(show_tm = 1; syncTools());
    return false;
#undef RL
}

/* a ribbon / menu command this program has: greyed otherwise (its behaviour is not ported). A parent counts only when
   it or at least one child works */
bool MainWindow::cmdOk(const UiCmd &c) {
    rlFt_ = c.ft; rlFtSet_ = true;
    rlNoGeo_ = !c.dll.isEmpty() && !c.dll.startsWith("MCCAD", Qt::CaseInsensitive);   /* wireframe commands live in MCCAD.DLL / MCCADUTIL.DLL */
    bool ok = localRun(c.text, true) || localRun(c.tip, true);
    rlFtSet_ = false; rlNoGeo_ = false;
    if (!ok && !c.ft.isEmpty()) ok = mcFunction(c.ft, true);   /* the command's FT function as the QAT / keymap run it (mcamui.c run_mc_function) */
    for (int i = 0; !ok && i < c.sub.size(); i++) ok = cmdOk(c.sub[i]);
    return ok;
}

void MainWindow::invoke(const UiCmd &c) {
    if (g_regen_busy) return;                            /* OK's computation runs (opdlg.cpp): no command may change the part */
    if (ns_open || ns_cfg_open || ns_res_open) return;   /* Geometry Nesting's windows are modal (DoModal) */
    if (!cmdOk(c)) return;                               /* greyed: not ported */
    if (srf_cmd) view_->withGL([] { srf_end(); });       /* another function ends a Surfaces command (its undo step) */
    addRecent(c.text.isEmpty() ? c.tip : c.text);
    rlFt_ = c.ft; rlFtSet_ = true;
    rlNoGeo_ = !c.dll.isEmpty() && !c.dll.startsWith("MCCAD", Qt::CaseInsensitive);
    bool done = localRun(c.text, false) || localRun(c.tip, false);
    rlFtSet_ = false; rlNoGeo_ = false;
    if (!done && !c.ft.isEmpty()) { mcFunction(c.ft, false); refreshAll(); }
}

/* mcamui.c add_recent: newest first, up to config 4023 "Number of functions in the MRU menu?" (and 16) */
void MainWindow::addRecent(const QString &t) {
    if (t.isEmpty()) return;
    int i = recent_.indexOf(t);
    if (i >= 0) recent_.removeAt(i);
    else if (recent_.size() >= qMin(qc_mru_n, 16)) { if (recent_.isEmpty()) return; recent_.removeLast(); }
    recent_.prepend(t);
    if (rm_) rm_->refresh(recent_);
}

/* the FT functions of the QAT / keymap (mcamui.c run_mc_function), the part ported so far; the rest is greyed */
bool MainWindow::mcFunction(const QString &fn, bool probe) {
    if (g_regen_busy && !probe) return true;             /* OK's computation runs: taken, nothing done */
#define F(name, x) if (fn == name) { if (!probe) { x; view_->redraw(); } return true; }
    ViewInput *in = viewInput(view_);
    F("OnScreenFit", view_fit(1));                                   /* OnScreenFit -> FUN_1801ccbd0 */
    /* Machine tab > Simulator / Post (layout.txt 53731 / 53732 / 53733): the FT functions the Toolpaths manager toolbar 800
       buttons also run (OMbackplotSelectedOperations / OMverifySelectedOperations / OMpostSelectedOperations) */
    F("OMbackplotSelectedOperations", view_->withGL([] { bp_start(); if (bp_on) bp_frame_still(); }); syncMisc());
    F("OMverifySelectedOperations", view_->withGL([] { vf_start(); if (vf_on) vf_frame_still(); }); updateStatusShade(); syncMisc());
    F("OMpostSelectedOperations", post_ops(); refreshAll());
    F("OnScreenZoom", zoom_window_start());                          /* F1: the zoom window (core_ui cui_misc.c) */
    F("OnScreenUnzoom", view_unzoom(0.8));                           /* OnScreenUnzoom: UnzoomByScale(0.8) */
    F("OnScreenUnzoomPrevious", view_unzoom_previous());             /* FUN_180329a10 */
    F("OnViewPreviousView", in->viewPop());
    /* reserved views (ReservedPlanes::BuildStandardMatrices), all through CommonPlaneMenu -> FUN_1801ccd70 (animated,
       config 33006); Back / Bottom / Left are commands 0x81a6-0x81a8 -> views 3 / 4 / 6 (decomp/mastercam_viewblr.c) */
    F("OnViewTop", in->viewPush(); view_std_animated(1));            /* CommonPlaneMenu 0x8024 -> FUN_1801ccd70 */
    F("OnViewFront", in->viewPush(); view_std_animated(2));          /* CommonPlaneMenu 0x8025 */
    F("OnViewBack", in->viewPush(); view_std_animated(3));           /* CommonPlaneMenu 0x81a6 */
    F("OnViewBottom", in->viewPush(); view_std_animated(4));         /* CommonPlaneMenu 0x81a7 */
    F("OnViewRight", in->viewPush(); view_std_animated(5));          /* CommonPlaneMenu 0x8026 */
    F("OnViewLeft", in->viewPush(); view_std_animated(6));           /* CommonPlaneMenu 0x81a8 */
    F("OnViewIsometric", in->viewPush(); view_std_animated(7));      /* CommonPlaneMenu 0x8027 */
    if (fn == "OnScreenRepaint" || fn == "OnScreenRegenerate") { if (!probe) view_->withGL([] { if (loaded) scene_reupload(); }); return true; }
    /* the display modes (S-15, decomp/shade15/handlers.c): OnShadeWire / OnShadeShaded / OnShadeShadedOutline -> DoShadeWire (render_cfg 1)
       / DoShadeShaded (3) / DoShadeShadedOutline (4); the status bar's OnStatusDisplayWireAll / ShadedAll / ShadedOutlined post those
       (post_FT_command "OnShadeWire" / "OnShadeShaded" / "OnShadeShadedOutline", 0x1804718a0 / 0x1804717e0 / 0x180471840). Dimmed / No
       Hidden (render_cfg 2) are not ported. */
    F("OnShadeWire", scene_shade(1); updateStatusShade());
    F("OnShadeShaded", scene_shade(3); updateStatusShade());
    F("OnShadeShadedOutline", scene_shade(4); updateStatusShade());
    F("OnStatusDisplayWireAll", scene_shade(1); updateStatusShade());
    F("OnStatusDisplayShadedAll", scene_shade(3); updateStatusShade());
    F("OnStatusDisplayShadedOutlined", scene_shade(4); updateStatusShade());
    F("OnShadeToggle", scene_shade_toggle(); updateStatusShade());              /* Alt+S, 0x180471590 */
    F("OnShadeTranslucentToggle", scene_translucent_toggle(); updateStatusShade());   /* Ctrl+T, 0x1804715f0: only with shading on */
    F("OnHideToolpath", show_tp = !show_tp);
    F("OnWCSAxesToggle", show_axes = !show_axes);
    F("OnDisplayInfo", show_info = !show_info; show_info_axes = show_info);   /* F9 "Show Axes": OnDisplayInfo flips show_part_info, read only by axis drawing (mcamui) */
    F("OnScreenGrid", show_grid = !show_grid);                       /* Alt+G: grid on / off */
    F("OnScreenLevels", showManager(3));
    F("ViewOpManager", showManager(0, true));
    F("ViewSolidsManager", showManager(1));
    F("ViewPlanesManager", showManager(2));
    /* selection / undo (core_ui, mcamui.c run_mc_function) */
    if (fn == "SelectAllMask") { if (!probe) { sel_all(); redrawView(); } return true; }
    /* OnEditCut / Copy / Paste (FUN_180261310 / FUN_180267ca0): the Toolpaths manager focused -> Cut / Copy (Shift: append) /
       Paste of operations, else the entity clipboard (formats/align_ops.md 2) */
    if ((fn == "OnEditCopy" || fn == "OnEditCut" || fn == "OnEditPaste") && tpmFocused()) {
        if (!probe) { bool sh = QApplication::keyboardModifiers() & Qt::ShiftModifier;
            view_->withGL([&] { if (fn == "OnEditPaste") op_paste(); else op_copy_ex(fn == "OnEditCut", sh); }); refreshAll(); }
        return true; }
    F("OnEditCopy", edit_copy());                                    /* the entity clipboard (core_ui cui_misc.c) */
    if (fn == "OnEditCut") { if (!probe) { view_->withGL([] { edit_cut(); }); refreshAll(); } return true; }
    F("OnEditPaste", edit_paste());                                  /* the Paste prompt (5457); Enter / Esc continue */
    if (fn == "AnalyzeStart") { if (!probe) { view_->withGL([] { analyze_start(); }); refreshAll(); } return true; }   /* F4 (core_ui cui_analyze.c, mccadutilres 6333) */
    F("ConfigureX", show_gridset = 1; syncMisc());                   /* the Screen Grid page (1589) */
    /* Ctrl+Z with the Toolpaths manager focused = Undelete (COpMgrTree::ExecuteKeymap 'Z' -> OMOnUnDelete); else Undo (geometry only) */
    if (fn == "UndoEvent" && tpmFocused()) { if (!probe) { view_->withGL([] { op_undelete(); }); refreshAll(); } return true; }
    if (fn == "UndoEvent") { if (!probe) { view_->withGL([] { do_undo(); }); refreshAll(); } return true; }   /* an undo of a live step ends it (geo_live_valid) */
    if (fn == "RedoEvent") { if (!probe) { view_->withGL([] { do_redo(); }); refreshAll(); } return true; }
    if (fn == "delete_entities") { if (!probe) { view_->withGL([] { delete_selected_ents(); }); refreshAll(); } return true; }
    if (fn == "OnHide") { if (!probe) { view_->withGL([] { hide_toggle(); }); redrawView(); } return true; }   /* Alt+E */
    if (fn == "SbSelectRotationPosition") { if (!probe) { rotposPick_ = true; snprintf(cam_msg, sizeof cam_msg, "Select a center point for Motion Controller rotation."); redrawView(); } return true; }
    if (fn == "set_main_from_entity") { if (!probe) set_main_from_entity(); return true; }
    /* Home > Attributes / Organize (core_ui cui_attr.c; mastercam.dll ChangeSelected* / ChangeEntity* / OnScreen*, formats/home_tab_plan.md) */
    if (fn == "ChangeSelectedEntityLineStyle") { if (!probe) { view_->withGL([] { attr_line_style(-1); }); refreshAll(); } return true; }
    if (fn == "ChangeSelectedEntityLineWidth") { if (!probe) { view_->withGL([] { attr_line_width(-1); }); refreshAll(); } return true; }
    if (fn == "ChangeSelectedEntityPointStyle") { if (!probe) { view_->withGL([] { attr_point_style(-1); }); refreshAll(); } return true; }
    if (fn == "ChangeSelectedWireframeColor" || fn == "ChangeSelectedWireframeColorWithPicker") { if (!probe) { attr_color_cmd(0); syncMisc(); } return true; }
    if (fn == "ChangeSelectedSurfacesColor" || fn == "ChangeSelectedSurfacesColorWithPicker") { if (!probe) { attr_color_cmd(1); syncMisc(); } return true; }
    if (fn == "ChangeSelectedSolidsColor" || fn == "ChangeSelectedSolidsColorWithPicker") { if (!probe) { attr_color_cmd(3); syncMisc(); } return true; }
    if (fn == "ChangeEntityLevel") { if (!probe) { selrun_start(SR_LEVEL); refreshAll(); } return true; }
    if (fn == "ChangeEntityAttribs") { if (!probe) { selrun_start(SR_ATTRIBS); refreshAll(); } return true; }
    if (fn == "OnScreen2D3DToggle") { if (!probe) { constr_toggle_3d(); syncHome(); } return true; }
    if (fn == "OnScreenZDepth") { if (!probe) { zd_start(); refreshAll(); } return true; }
    if (fn == "ToClipboard") { if (!probe) { copyImagePick_ = true; ciN_ = 0; snprintf(cam_msg, sizeof cam_msg, "Select an area of the screen to copy\nPress <Esc> to select entire screen"); refreshAll(); } return true; }   /* opmanres 3288 */   /* mini toolbar Set From Entity (core_ui) */
    if (fn == "ClearColors") { if (!probe) { view_->withGL([] { xf_clear_colors(); }); redrawView(); } return true; }   /* mastercam.dll ClearColors: the group / result marks + selection off (core_ui cui_xform.c) */
    if (fn == "doAutocursorDlg") { if (!probe) showAcSettings(); return true; }             /* command 3340 (opmanres 5880): dialog 779 */
    F("OnFileNew", request(ACT_NEW));
    F("OnFileOpen", request(ACT_OPEN));
    F("OnFileSave", docSave(false));
    F("OnFileSaveAs", docSave(true));
    F("OnFileExit", request(ACT_EXIT));
    F("OnFilePrint", want_print = 1; syncMisc());                   /* INTENTIONAL: a BMP of the window at the next frame (core_ui print_path; syncMisc) */
    F("OnHelp", show_help = !show_help; syncMisc());                /* INTENTIONAL: the keymap window (formats/ui.md) */
    F("OnAppAbout", show_about = 1; syncMisc());                    /* About apprenticeCAM (ours) */
    /* doDraft_GLOBALS / run_user_app / AutoSaveParams / ToggleMultiThreadDialog / ViewArtManager: mcamui binds them to a no-op;
       they have no behaviour here: greyed (the parity check lists them as no-ops) */
    return false;
#undef F
}

/* ------------------------------------------------------------------------------------------------ ribbon */
/* the original's palette tooltip: the item name, then the description wrapped to the window (the 320 px wrap is ours,
   mcamui.c gal_tooltip); the descriptions come from the user's own install (gallery_tips.tsv, core_ui gal_tip) */
QString MainWindow::galleryTip(const QString &key, const QString &name) const {
    const char *d = gal_tip(key.toUtf8().constData());
    if (!d || !d[0]) return name;
    QFontMetrics fm(QToolTip::font()); QStringList lines; lines << name;
    QString rest = QString::fromUtf8(d);
    while (!rest.isEmpty() && lines.size() < 9) {          /* the longest prefix <= 320 px, broken at a space (8 lines) */
        int best = 0, sp = -1;
        for (int k = 0; k < rest.size(); k++) { if (fm.horizontalAdvance(rest.left(k + 1)) > 320) break; best = k + 1; if (rest[k] == ' ') sp = k; }
        if (best < rest.size() && sp > 0) best = sp;
        if (!best) break;
        lines << rest.left(best); rest = rest.mid(best); while (rest.startsWith(' ')) rest.remove(0, 1);
    }
    return lines.join('\n');
}
void MainWindow::showGalleryPopup() {                    /* show:gallery - the Toolpaths tab's first palette open (tests, mcamui open_gallery) */
    for (auto &g : gals_) if (g.first == machine_ + "/Toolpaths" || g.first == "Toolpaths") { setTabByName("Toolpaths"); QApplication::processEvents(); g.second->showMoreDetail(); return; }
}
QAction *MainWindow::makeAction(const UiCmd &c, int px) {
    QString lab = c.label();
    QAction *a = new QAction(Icons::icon(lab, px, px), lab, this);
    a->setToolTip(c.desc.isEmpty() ? c.tip : c.desc);
    a->setEnabled(cmdOk(c));
    UiCmd copy = c;
    connect(a, &QAction::triggered, this, [this, copy] { invoke(copy); });
    acts_.append(ActRef{ a, c });
    return a;
}

void MainWindow::addCmd(SARibbonPanel *panel, const UiCmd &c, bool large) {
    /* Home > Attributes / Organize combos (core_ui cui_attr.c): the point / line style and width bitmap combos (CBmpCombo2), the Z depth
       (RibbonZDepthComboBox: a typed depth = OnScreenZDepth's 'V' value) and the main level (typed; the Levels list: ours) */
    if (c.kind == "ComboBox" && (c.id == "53409" || c.id == "53410" || c.id == "53411" || c.id == "53422" || c.id == "53424")) {
        int id = c.id.toInt(); QComboBox *cb;
        if (id <= 53411) {
            cb = makeAttrCombo(this, id == 53409 ? 2 : id == 53410 ? 0 : 1); cb->setToolTip(c.label()); cb->setFixedWidth(id == 53409 ? 58 : 88);
            connect(cb, QOverload<int>::of(&QComboBox::activated), this, [this, cb, id](int) {
                int v = cb->currentData().toInt();
                view_->withGL([id, v] { if (id == 53409) attr_point_style(v); else if (id == 53410) attr_line_style(v); else attr_line_width(v); });
                refreshAll(); syncHome(); });
        } else {
            cb = new QComboBox(this); cb->setEditable(true); cb->setToolTip(c.label()); cb->setFixedWidth(id == 53422 ? 90 : 70);
            connect(cb->lineEdit(), &QLineEdit::returnPressed, this, [this, cb, id] {
                bool ok = false; double v = cb->currentText().toDouble(&ok); if (!ok) { syncHome(); return; }
                view_->withGL([id, v] { if (id == 53422) zd_set(v); else if (v >= 1) geo_level = (int)v; });
                refreshAll(); syncHome(); });
        }
        homeCombos_.append({ id, cb });
        panel->addSmallWidget(cb);
        return;
    }
    QAction *a = makeAction(c, large ? 32 : 16);
    QToolButton::ToolButtonPopupMode pm = QToolButton::DelayedPopup;
    if (!c.sub.isEmpty()) {                             /* split button: the arrow opens the list; no entry = the list only */
        QMenu *m = new QMenu(this);
        for (const UiCmd &sc : c.sub) m->addAction(makeAction(sc, 16));
        a->setMenu(m);
        pm = c.entry.isEmpty() ? QToolButton::InstantPopup : QToolButton::MenuButtonPopup;
    }
    if (large) panel->addLargeAction(a, pm); else panel->addSmallAction(a, pm);
}

void MainWindow::buildRibbon() {
    SARibbonBar *rb = ribbonBar();
    rb->applicationButton()->setText("File");
    connect(rb, &SARibbonBar::applicationButtonClicked, this, [this] { showBackstage(stack_->currentWidget() != backstage_); });
    for (int ti = 0; ti < ui_.tabs.size(); ti++) {
        const UiTab &t = ui_.tabs[ti];
        SARibbonCategory *cat;
        if (t.context.isEmpty()) cat = rb->addCategoryPage(t.name);
        else {
            SARibbonContextCategory *cc = ctx_.value(t.context);
            if (!cc) { cc = rb->addContextCategory(t.context, CONTEXT_FILL, t.context); ctx_.insert(t.context, cc); }
            cat = cc->addCategoryPage(t.name);
        }
        cats_ << cat;
        for (const UiGroup &g : t.g) {
            SARibbonPanel *panel = cat->addPanel(g.name);
            for (const UiElem &e : g.el) {
                if (e.is_gallery) {                     /* inline gallery: the gallery's own image size (GAL iw x ih, 64 x 32) */
                    SARibbonGallery *gal = panel->addGallery();
                    QList<QAction *> items;
                    for (int i = 0; i < e.items.size(); i++) {
                        /* the palette key: "gallery|name", "#2" ... when the name repeats within the gallery (mcamui.c gal_key) */
                        int occ = 1; for (int k = 0; k < i; k++) if (e.items[k].name == e.items[i].name) occ++;
                        QString key = occ > 1 ? QString("%1|%2#%3").arg(e.gname, e.items[i].name).arg(occ) : QString("%1|%2").arg(e.gname, e.items[i].name);
                        /* creating operations from the gallery: the items this program generates (mcamui.c draw_ribbon:
                           ok = cam_type_from_label) start their chaining (core_ui gallery_pick, phase 4; the op dialog after
                           the chaining's OK is phase 6); the others greyed - the gallery view draws actions with their
                           Normal image, so the faded one is given (icon_tint 80) */
                        QIcon gi = Icons::icon(key, e.iw > 0 ? e.iw : 32, e.ih > 0 ? e.ih : 32);
                        QSize gs(e.iw > 0 ? e.iw : 32, e.ih > 0 ? e.ih : 32);
                        QString nm = e.items[i].name;
                        /* srough: the item's type by its gallery (cam2d.c cam_type_from_gallery: the 3D galleries' "Pocket" /
                           "Parallel" are the classic surface roughing ops, not the 2D types of that name); a 3D type is started
                           by its type label */
                        int gt = cam_type_from_gallery(e.gname.toUtf8().constData(), nm.toUtf8().constData(), occ);
                        /* the Turning tab's Face (General Lathe Turning gallery, cmd_labels "lathe_face" = menu 3680's FT lathe_face
                           0x1801a3290): the new Lathe Face (core_ui cui_lface.c); the gallery's other lathe items stay greyed */
                        if (e.gname.contains("Lathe Turning") && nm == "Face" && occ == 1) {
                            QIcon gi2 = Icons::icon(key, e.iw > 0 ? e.iw : 32, e.ih > 0 ? e.ih : 32);
                            QAction *a = new QAction(gi2, nm, this);
                            a->setToolTip(galleryTip(key, nm));
                            connect(a, &QAction::triggered, this, [this] { view_->withGL([] { lface_open(); }); refreshAll(); syncMisc(); });
                            galActs_ << qMakePair(a, nm); galKeys_ << (t.name + (t.context.isEmpty() ? QString() : "@" + t.context) + "|" + key); items << a; continue;
                        }
                        bool ok = gt != 0;
                        QString pick = gt && cam_type_is_3d(gt) ? QString::fromUtf8(cam_type_label(gt)) : nm;
                        QAction *a = new QAction(ok ? gi : QIcon(gi.pixmap(gs, QIcon::Disabled)), nm, this);
                        a->setToolTip(galleryTip(key, nm));   /* the item name + its gallery_tips.tsv description (mcamui gal_tooltip, core_ui gal_tip) */
                        a->setEnabled(ok);
                        if (ok) connect(a, &QAction::triggered, this, [this, pick] { if (galleryPick(pick)) addRecent(pick); });
                        galActs_ << qMakePair(a, nm); galKeys_ << (t.name + (t.context.isEmpty() ? QString() : "@" + t.context) + "|" + key);
                        items << a;
                    }
                    SARibbonGalleryGroup *gg = gal->addCategoryActions(e.gname, items);
                    gals_ << qMakePair(t.context.isEmpty() ? t.name : t.context + "/" + t.name, gal);
                    /* mcamui.c draw_ribbon: two rows of icons (no labels; ENABLE_LABELS is the dropdown palette's), cells =
                       the image size + 2 / + 1, per row = INITIAL_COLUMNS, then the expand button */
                    gg->setGalleryGroupStyle(SARibbonGalleryGroup::IconOnly);
                    gg->setDisplayRow(SARibbonGalleryGroup::DisplayTwoRow);
                    gg->setGridMinimumWidth((e.iw > 0 ? e.iw : 32) + 2);
                    gg->setGridMaximumWidth((e.iw > 0 ? e.iw : 32) + 2);
                    gal->setCurrentViewGroup(gg);           /* the view group copies the settings when it is set */
                    int per_row = e.cols > 0 ? e.cols : 4;
                    gal->setFixedWidth(per_row * ((e.iw > 0 ? e.iw : 32) + 2) + 20);
                    continue;
                }
                const UiCmd &c = e.cmd;
                if (c.kind == "Group") {                /* a stack of small controls (all of them: Home > Attributes has a group of six) */
                    for (int s = 0; s < c.sub.size(); s++) addCmd(panel, c.sub[s], false);
                    continue;
                }
                addCmd(panel, c, !c.icon_l.isEmpty());  /* large button when it has a large image, else small (3 per column) */
            }
        }
    }
    rb->setContextCategoryColorHighLight([](const QColor &) { return CONTEXT_GREEN; });
    rb->setContextCategoryTitleTextColor(CONTEXT_TEXT);
    updateContexts();
}

/* the Quick Access Toolbar of the user's CONFIG/Mastercam.Workspace: QuickAccessToolbarCommands (u16 count, then
   u32 command ids, 0 = separator) resolved by QATFunctionTable ("id:App.Function,..."); text = command menu string
   (mcamui.c draw_qat) */
void MainWindow::buildQat() {
    char wsp[1100]; QFile f(QString::fromUtf8(qc_site_file(wsp, sizeof wsp, "user", "CONFIG/Mastercam.Workspace")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString table, cmds;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine());
        int p;
        if ((p = line.indexOf("<QATFunctionTable>\"")) >= 0) { int s = p + 19, e = line.indexOf('"', s); if (e > s) table = line.mid(s, e - s); }
        if ((p = line.indexOf("<QuickAccessToolbarCommands>\"")) >= 0) { int s = p + 29, e = line.indexOf('"', s); if (e > s) cmds = line.mid(s, e - s); }
    }
    SARibbonQuickAccessBar *qab = ribbonBar()->quickAccessBar();
    int nq = 0;
    for (int o = 4; o + 8 <= cmds.size() && nq < 16; o += 8) {     /* little-endian u32 per 8 hex digits after the u16 count */
        bool ok; unsigned b[4];
        for (int k = 0; k < 4; k++) b[k] = cmds.mid(o + 2 * k, 2).toUInt(&ok, 16);
        unsigned id = b[0] | b[1] << 8 | b[2] << 16 | b[3] << 24;
        if (!id) { qab->addSeparator(); nq++; continue; }
        QString key = QString("%1:").arg(id); int q = 0, hit = -1;
        while ((q = table.indexOf(key, q)) >= 0) { if (q == 0 || table[q - 1] == ',') { hit = q + key.size(); break; } q++; }
        if (hit < 0) continue;
        int dot = table.indexOf('.', hit), end = table.indexOf(',', hit); if (end < 0) end = table.size();
        if (dot < 0 || dot > end) continue;
        QString fn = table.mid(hit, 10) == "Mastercam." ? table.mid(dot + 1, end - dot - 1) : table.mid(hit, end - hit);   /* add-in: App.Function */
        QAction *a = new QAction(Icons::icon(fn), cmdLabel(fn), this);
        a->setEnabled(mcFunction(fn, true));          /* unported: greyed, no message (item 17) */
        connect(a, &QAction::triggered, this, [this, fn] { mcFunction(fn, false); });
        qab->addAction(a); qat_ << qMakePair(a, fn);
        nq++;
    }
}

void MainWindow::updateEnabled() {
    for (auto &r : acts_) r.a->setEnabled(cmdOk(r.c));
    if (qEnvironmentVariableIsSet("APPRENTICECAM_DEBUG_CMDS"))
        for (auto &r : acts_) fprintf(stderr, "cmd %-40s %s\n", r.c.label().toUtf8().constData(), r.a->isEnabled() ? "on" : "greyed");
    for (auto &q : qat_) q.first->setEnabled(mcFunction(q.second, true));
    if (tpm_) tpm_->updateEnabled();
}

/* k-th visible tab: main tabs then the machine's context tabs (mcamui.c visible_tab) */
void MainWindow::updateContexts() {
    for (auto it = ctx_.begin(); it != ctx_.end(); ++it) {
        if (it.key() == machine_) ribbonBar()->showContextCategory(it.value());
        else ribbonBar()->hideContextCategory(it.value());
    }
}

void MainWindow::setTabByName(const QString &name) {
    for (int i = ui_.tabs.size() - 1; i >= 0; i--)    /* the machine's context tab first (Toolpaths) */
        if (ui_.tabs[i].name == name && (ui_.tabs[i].context.isEmpty() || ui_.tabs[i].context == machine_)) { ribbonBar()->raiseCategory(cats_[i]); return; }
}
QString MainWindow::currentTabName() const {
    SARibbonTabBar *tb = ribbonBar()->ribbonTabBar();
    return tb ? tb->tabText(tb->currentIndex()) : QString();
}

/* ------------------------------------------------------------------------------------------------ docks */
/* the left dock column: the managers (Mastercam's UIDialogbarTabbedControlBar: Toolpaths, Solids, Planes, Levels,
   Recent Functions - mcamui.c draw_manager, MGR_W 330 px), tabs at the bottom. */
void MainWindow::buildDocks() {
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::ActiveTabHasCloseButton, false);   /* the manager tab names fit (mcamui: no tab close buttons) */
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaDynamicTabsMenuButtonVisibility, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::TabsAtBottom, true);   /* the managers' tabs are at the bottom (mcamui.c "mgrtabs") */
    QWidget *host = new QWidget(stack_);
    dock_ = new ads::CDockManager(host);
    auto *lay = new QVBoxLayout(host); lay->setContentsMargins(0, 0, 0, 0); lay->addWidget(dock_);
    stack_->addWidget(host);
    view_ = createView(nullptr);
    auto *central = new ads::CDockWidget(dock_, "Graphics");
    central->setWidget(view_->widget(), ads::CDockWidget::ForceNoScrollArea);
    central->setFeature(ads::CDockWidget::NoTab, true);
    dock_->setCentralWidget(central);
    ViewInput *vin = viewInput(view_);
    /* the auto-highlight rule (core_ui cui_autohl_active): idle general selection (config 4001) unless a point pick runs */
    vin->hlActive = [this] { CuiHlState s = {}; s.blocked = stack_->currentWidget() == backstage_ || show_tm || (cdlg_open && !cdlg_picking) || (ldlg_open && !ldlg_picking) || bpo_open;
                             s.point_pick = rotposPick_ || ex_pick || lk_pick || az_pick || st_point_pick(); s.entity_pick = cdlg_picking || ldlg_picking || analyze_pick || srf_cmd || tb_pick == 1;   /* + a Surfaces command's get_event (cui_surf.c) */   /* mcamui autohl_active */
                             s.ex_mode = ex_mode; s.ex_phase = ex_phase; s.cam_chaining = (cam_mode && !show_params) || geo_chaining;   /* chaining (phase 4), a wireframe chain command's chain_it */
                             s.geo_cmd = geo_cmd; s.geo_need = geo_cmd ? geo_need() : 0; return cui_autohl_active(&s) != 0; };
    /* a drawing command takes its pick where the button went down (OnLButtonDown FUN_1801ca2c0 posts 0x8030 with the
       press point, decomp/mastercam_viewmouse.c; mcamui.c main loop geo_pick_now); chaining picks (chain_it: toolpath
       chaining, the Extrude command's chaining) come through the same view message, so they take the press point too */
    vin->pressPick = [] { return (geo_cmd && geo_need() && !(ex_mode && ex_phase)) || chain_press_pick(); };
    connect(vin, &ViewInput::clicked, this, [this](float x, float y) { viewClick(x, y); });
    connect(vin, &ViewInput::contextMenu, this, [this](QPoint g) { viewMenu(g); });
    connect(vin, &ViewInput::chainChanged, this, [this] { refreshAll(); });
    connect(vin, &ViewInput::zoomed, this, [this] { refreshAll(); });
    connect(vin, &ViewInput::extrudeChanged, this, [this] { redrawView(); if (geo_cmd == G_BBOX && !bb_dragging()) refreshAll(); });   /* the Bounding Box panel's sizes after a push-pull commit */   /* an arrow drag moved / ended (core_ui ex_drag_*) */
    connect(vin, &ViewInput::cursorWorld, this, [this](double x, double y, double, bool in, bool) {
        /* X / Y panes: ribbonui.dll templates "X:   0000.00000" (empty "X:   0.00000") -> 5 decimals */
        sx_->setText(QString::asprintf("X:   %.5f", in ? x : 0.0)); sy_->setText(QString::asprintf("Y:   %.5f", in ? y : 0.0));
    });
    CommandSink sink{ [this](const QString &t) { return localRun(t, true); },
                      [this](const QString &t) { UiCmd c; c.text = t; c.tip = t; invoke(c); } };
    tpm_ = new ToolpathsManager(ui_, sink, nullptr);
    sm_ = new SolidsManager(nullptr);
    auto *pm = new PlanesManager(nullptr); pm_ = pm;
    lm_ = new LevelsManager(nullptr);
    /* Recent Functions: run_local, else gallery_pick (mcamui.c draw_manager's recent list) */
    rm_ = new RecentManager(CommandSink{ [this](const QString &t) { return localRun(t, true) || cam_type_from_label(t.toUtf8().constData()) != 0; },
                                         [this](const QString &t) { UiCmd c; c.text = t; c.tip = t; if (cmdOk(c)) invoke(c); else galleryPick(t); } }, nullptr);
    connect(tpm_, &ToolpathsManager::selectionChanged, this, [this] { view_->redraw(); });
    connect(lm_, &LevelsManager::levelsChanged, this, [this] { refreshAll(); });   /* a level shown / hidden: redraw (FUN_18047bcc0) */
    connect(tpm_, &ToolpathsManager::paramsRow, this, [this](int op) { openOpParams(op); });
    connect(tpm_, &ToolpathsManager::propsRow, this, [this](int page) { st_open(page); syncMisc(); });   /* EditGroup (core_ui cui_stock.c) */
    connect(tpm_, &ToolpathsManager::menuRun, this, [this](int item) { opMenuRun(item); });
    tpm_->menuHas = [](int) { return true; };          /* every front-end item exists since phase 8 (Edit Common Parameters, Rename) */
    connect(tpm_, &ToolpathsManager::renamed, this, [this] { renameShown_ = 0; refreshAll(); }, Qt::QueuedConnection);
    connect(tpm_, &ToolpathsManager::renameBegun, this, [this] { syncMisc(); });   /* the op's editor (syncMisc: beginOpRename) */   /* queued: the editor also closes while the window is destroyed */
    connect(tpm_, &ToolpathsManager::toolRow, this, [this](int op) {   /* the tree's tool row: the Tool Manager on that tool (mcamui draw_manager) */
        show_tm = 1; for (int k = 0; k < F.ntools; k++) if (F.tools[k].num == F.ops[op].tool) tm_part_sel = k;
        syncTools(); });
    sm_->withGL = [this](const std::function<void()> &fn) { view_->withGL(fn); };
    connect(sm_, &SolidsManager::acted, this, [this] { refreshAll(); });
    static const char *mtabs[] = { "Toolpaths", "Solids", "Planes", "Levels", "Recent Functions" };
    QWidget *w[5] = { tpm_, sm_, pm, lm_, rm_ };
    ads::CDockAreaWidget *area = nullptr;
    for (int i = 0; i < 5; i++) {
        auto *d = new ads::CDockWidget(dock_, mtabs[i]);
        d->setWidget(w[i], ads::CDockWidget::ForceNoScrollArea);
        d->tabWidget()->setElideMode(Qt::ElideNone);   /* full names (mcamui: the tab is the text width + 12) */
        if (!area) area = dock_->addDockWidget(ads::LeftDockWidgetArea, d);
        else dock_->addDockWidgetTabToArea(d, area);
        mgrDocks_ << d;
    }
    mgrArea_ = area;
    /* the running wireframe command's function panel: a tab between Levels and Recent Functions in the same dock (the
       Mastercam.Workspace dock blob: UIDialogbarTabbedControlBar; mcamui.c draw_manager "mgrtabs"), shown while a command
       runs; a click on a manager tab shows that manager, the panel's tab brings the panel back (dock_show_panel) */
    geo_ = new GeoPanel(nullptr);
    geo_->withGL = [this](const std::function<void()> &fn) { view_->withGL(fn); };
    connect(geo_, &GeoPanel::changed, this, [this] { redrawView(); });
    connect(geo_, &GeoPanel::button, this, [this](int c) { geoButton(c); });
    geoDock_ = new ads::CDockWidget(dock_, "Function panel");
    geoDock_->setWidget(geo_, ads::CDockWidget::ForceNoScrollArea);
    geoDock_->tabWidget()->setElideMode(Qt::ElideNone);
    dock_->addDockWidgetTabToArea(geoDock_, area, 4);
    geoDock_->toggleView(false);
    /* the Extrude panel (CreateInFunctionPnl, formats/extrude.md 1.1 step 4): docked in the same function-panel pane as
       the wireframe panels, titled sld 846 "Solid Extrude"; shown while the panel is up (ex_mode && ex_phase - not during
       the chain selection, mcamui left_dock_w / draw_extrude_panel) */
    ex_ = new ExtrudePanel(nullptr);
    connect(ex_, &ExtrudePanel::changed, this, [this] { redrawView(); });
    connect(ex_, &ExtrudePanel::button, this, [this](int c) { exButton(c); });
    exDock_ = new ads::CDockWidget(dock_, "Solid Extrude");
    exDock_->setWidget(ex_, ads::CDockWidget::ForceNoScrollArea);
    exDock_->tabWidget()->setElideMode(Qt::ElideNone);
    dock_->addDockWidgetTabToArea(exDock_, area, 5);
    exDock_->toggleView(false);
    /* the Constant Fillet panel (CreateInFunctionPnl(dialog, sld 823), formats/solid_fillet.md 1.1 step 4) and, during the pick, the Solid
       Selection dialog (1413) in the same dock (ours: Mastercam shows that dialog as its own modeless window) */
    fl_ = new FilletPanel(nullptr);
    connect(fl_, &FilletPanel::changed, this, [this] { redrawView(); });
    connect(fl_, &FilletPanel::button, this, [this](int c) { flButton(c); });
    flDock_ = new ads::CDockWidget(dock_, "Constant Radius Fillet");
    flDock_->setWidget(fl_, ads::CDockWidget::ForceNoScrollArea);
    flDock_->tabWidget()->setElideMode(Qt::ElideNone);
    dock_->addDockWidgetTabToArea(flDock_, area, 6);
    flDock_->toggleView(false);
    mgrDocks_[0]->setAsCurrentTab();
    dock_->setSplitterSizes(area, { 385, 1215 });       /* mcamui MGR_W 330 (ours) + room for ADS's tab margins */
}

void MainWindow::showManager(int tab, bool toggle) {
    if (tab < 0 || tab >= mgrDocks_.size()) return;
    ads::CDockWidget *d = mgrDocks_[tab];
    if (toggle && !d->isClosed() && d->isCurrentTab()) { d->toggleView(false); return; }   /* ViewOpManager: hide when it is the shown tab */
    if (d->isClosed()) d->toggleView(true);
    d->setAsCurrentTab();
}
void MainWindow::setManagerTab(int k) { showManager(k); }

/* ------------------------------------------------------------------------------------------------ status bar */
/* status bar = the STATUS_BAR of the BCGP ribbon XML (ribbonuires.dll): X / Y panes (RibbonStatusBarPane, ribbonui.dll
   templates "X:   0000.00000" / empty "X:   0.00000" -> 5 decimals), Z depth (Select Z Depth button + RibbonZDepthComboBox
   "Z:"), CPlane 2D/3D, CPLANE / TPLANE / WCS popups, then the display buttons (IDR_BCGP_IMAGE_STATUS_GROUP_5, 14 px cells:
   Wireframe, Dimmed, No Hidden, Outline Shaded, Shaded, Translucency). The popup buttons' runtime face text format is NOT
   DECODED (the panes live in ribbonui.dll, no Ghidra project): shown as their XML name + the active plane's name (MastercamPlanes::GetActiveName 0x1802c56d0, the same name the buttons' tooltips show: FUN_180488b40) - syncHome. */
void MainWindow::buildStatus() {
    QStatusBar *sb = statusBar();
    sx_ = new QLabel("X:   0.00000"); sx_->setMinimumWidth(130);
    sy_ = new QLabel("Y:   0.00000"); sy_->setMinimumWidth(130);
    sz_ = new QLabel("Z:   0.00000"); sz_->setMinimumWidth(110);   /* construction depth (always 0 here) */
    /* UpdateStatusBarText (core_ui cui_status_text): pane 0's text, ours a label before the X / Y / Z panes, shown once set */
    sst_ = new QLabel; sst_->hide(); sb->addWidget(sst_);
    sb->addWidget(sx_); sb->addWidget(sy_); sb->addWidget(sz_);
    /* Backplot: UpdateStatusBarText "X:%1.4f   Y:%1.4f  Z:%1.4f" (backplot.md 4.4) replaces the panes while backplotting (mcamui draw_status) */
    sbp_ = new QLabel; sbp_->setMinimumWidth(400); sbp_->hide(); sb->addWidget(sbp_);
    d3_ = new QToolButton; d3_->setAutoRaise(true); d3_->setMinimumWidth(34); d3_->setToolTip(cmdLabel("OnScreen2D3DToggle"));   /* CPlane 2D / 3D (core_ui constr_toggle_3d) */
    d3_->setFixedHeight(sx_->sizeHint().height());       /* the bar's height as with the label it replaces (the view's size must not change) */
    connect(d3_, &QToolButton::clicked, this, [this] { mcFunction("OnScreen2D3DToggle", false); });
    sb->addWidget(d3_);
    static const char *pl[] = { "CPLANE", "TPLANE", "WCS" };
    for (int i = 0; i < 3; i++) { auto *l = new QLabel(QString("%1  Top").arg(pl[i])); l->setMinimumWidth(100); l->setAlignment(Qt::AlignCenter); sb->addWidget(l); splab_[i] = l; }
    /* N13-8: the Selection Bar's solid buttons GSSolidBody / GSSolidFace (ribbonuires IDR_BCGP_RIBBON_XML, the Home category's
       ELEMENTS outside its panels: ids 53394 "Select Solid body" / 53396 "Select Solid face", INDEX_SMALL 68 / 70 of
       IDR_BCGP_IMAGE_CATEGORY_SMALL_0; tooltips = their menu text, cmd_labels.tsv / opmanres 1917 / 1916) - checkable, each toggles
       its bit of the solid pick mask (mcgeomsld OnSolidsSelectBody @1800d1260 / OnSolidsSelectFace @1800d13a0), enabled only while
       a solid selection runs (OnUpdateSolidsSelectBody @1800d15c0). The Selection Bar itself (its place, its other controls) is NOT
       DECODED / NOT PORTED: ours, the two buttons sit in the status bar and are shown only while they are enabled (the drive /
       check prompt), so the window looks as before otherwise */
    static const char *sfn[2] = { "GSSolidBody", "GSSolidFace" };
    for (int i = 0; i < 2; i++) {
        auto *b = new QToolButton; b->setAutoRaise(true); b->setCheckable(true); b->setIconSize(QSize(16, 16));
        b->setIcon(Icons::icon(QString("#strip:IDR_BCGP_IMAGE_CATEGORY_SMALL_0.png:%1").arg(i ? 70 : 68), 16, 16));
        b->setToolTip(cmdLabel(sfn[i])); b->setFixedHeight(sx_->sizeHint().height());   /* the bar keeps its height */
        connect(b, &QToolButton::clicked, this, [this, i] { selBarClick(i == 1); });
        b->hide(); sb->addWidget(b); selBtn_[i] = b;
    }
    static const char *fn[] = { "OnStatusDisplayWireAll", "OnStatusDisplayWireDimmed", "OnStatusDisplayWireHidden", "OnStatusDisplayShadedOutlined", "OnStatusDisplayShadedAll", "OnShadeTranslucentToggle" };
    for (int i = 0; i < 6; i++) {
        auto *b = new QToolButton; b->setAutoRaise(true); b->setCheckable(true); b->setIconSize(QSize(14, 14));
        b->setIcon(Icons::icon(QString("#strip14:IDR_BCGP_IMAGE_STATUS_GROUP_5.png:%1").arg(i), 14, 14));
        b->setToolTip(cmdLabel(fn[i]));
        b->setEnabled(i == 0 || i == 3 || i == 4 || i == 5);    /* Dimmed / No Hidden: not ported (item 17) */
        connect(b, &QToolButton::clicked, this, [this, i] { mcFunction(fn[i], false); });   /* fn[] is static */
        sb->addPermanentWidget(b); shade_[i] = b;
    }
    updateStatusShade();
}
void MainWindow::updateSelBar() {
    bool on = ms_selbar_enabled() != 0;
    for (int i = 0; i < 2; i++) if (selBtn_[i]) { selBtn_[i]->setVisible(on); selBtn_[i]->setChecked(i ? ms_sld_faces : ms_sld_body); }
}
bool MainWindow::selBarClick(bool face) {
    if (!ms_selbar_enabled()) return false;
    ms_selbar_toggle(face ? 1 : 0); updateSelBar(); redrawView();
    return true;
}
bool MainWindow::selBarTestClick(bool face) {
    QToolButton *b = selBtn_[face ? 1 : 0];
    if (!b || b->isHidden() || !b->isEnabled()) return false;
    b->click(); return true;
}
/* N13-9: the generation queued by the flow of a new 3D op runs once the current input is handled (not inside the core call that
   queued it, which may hold the GL context) */
void MainWindow::queueGen() {
    if (genQueued_) return;
    genQueued_ = true;
    QTimer::singleShot(0, this, [this] { runPendingGen(); });
}
void MainWindow::runPendingGen() {
    genQueued_ = false;
    if (!ms_gen_waiting() || g_regen_busy) return;
    CamOp job; memset(&job, 0, sizeof job);
    if (!ms_gen_take(&job)) { refreshAll(); return; }
    if (opDlg_) opDlg_->runJob(job, [&job](int okv, int cancelled) { gen_end(&job, okv, cancelled); }, this);
    else view_->withGL([&job] { int okv = gen_run(&job); gen_end(&job, okv, 0); });   /* no op dialog was made: cannot happen in the flow */
    refreshAll(); syncOps();
}
void MainWindow::updateStatusShade() {
    if (!shade_[0]) return;
    /* OnUpdateDisplayWireAll / ShadedOutlined / ShadedAll (0x1804724e0 / 0x1804724c0 / 0x1804724a0): checked = render_cfg 1 / 4 / 3;
       OnUpdateShadeTranslucentToggle (0x1804727d0): enabled with render_cfg > 2, checked = gl_fulltime_translucent */
    int rc = scene_render_cfg();
    for (int i = 0; i < 6; i++) shade_[i]->setChecked((i == 0 && rc == 1) || (i == 3 && rc == 4) || (i == 4 && rc == 3) || (i == 5 && (solid_mode ? solid_mode == 2 : shade_transl)));
    shade_[5]->setEnabled(rc > 2);
}

/* ------------------------------------------------------------------------------------------------ document */
/* MFC SDI frame title: "<document> - <IDR_MAINFRAME title>" (opmanres string 128 "Mastercam" -> the program name); a
   product / version suffix (opmanres 104 / 105) is NOT DECODED, so not shown (mcamui.c draw_qat) */
void MainWindow::updateTitle() {
    QString p = doc_path[0] ? QString::fromUtf8(doc_path) : QString::fromUtf8(qc_part_path());   /* the project, else the part */
    QString base = p.isEmpty() ? QString() : QFileInfo(p).fileName();
    setWindowTitle(base.isEmpty() ? QString("apprenticeCAM") : base + " - apprenticeCAM");
}

void MainWindow::refreshAll() {
    tpm_->refresh(); sm_->refresh(); lm_->refresh(); rm_->refresh(recent_); backstage_->refresh(); if (pm_) pm_->refresh();
    updateTitle(); updateEnabled(); syncHome(); updateSelBar(); redrawView();
}
/* Home > Attributes / Organize state: the ribbon combos show the main attributes (the selection's values in Mastercam's own combos:
   NOT DECODED, ours: the main ones), the 2D / 3D button, the status bar's Z = the construction depth */
void MainWindow::syncHome() {
    for (auto &hc : homeCombos_) {
        QComboBox *cb = hc.second; int id = hc.first;
        if (id == 53409) setAttrCombo(cb, cfg_pt_style); else if (id == 53410) setAttrCombo(cb, cfg_ln_style); else if (id == 53411) setAttrCombo(cb, (int)cfg_ln_width);
        else if (!cb->lineEdit()->hasFocus()) cb->setEditText(id == 53422 ? QString::number(constr_depth, 'f', 4) : QString::number(geo_level));
    }
    if (d3_) d3_->setText(cfg_constr_3d ? "3D" : "2D");
    if (sz_) sz_->setText(QString("Z:   %1").arg(constr_depth, 0, 'f', 5));
    { static const char *pl[3] = { "CPLANE", "TPLANE", "WCS" }; static const int ty[3] = { 0, 1, 3 };
      for (int i = 0; i < 3; i++) if (splab_[i]) splab_[i]->setText(QString("%1  %2").arg(pl[i], QString::fromUtf8(qc_plane_active_name(ty[i])))); }
}
void MainWindow::syncMiscSlot() { syncMisc(); refreshAll(); }
/* the Plane Selection 1085 (core_ui cui_plsel.c) while get_plane_info runs; hidden while its X / Y / Z point pick runs (SelectionCue:
   AfxSuspendWindow); disabled while the plane list 842 of its Named is up (cui_planes.c pls_which 4: shown by syncOps) */
void MainWindow::syncPlaneSel() {
    if (geo_ && geo_->isEnabled() == (psel_open != 0)) geo_->setEnabled(!psel_open);   /* the caller's panel while get_plane_info runs (Ordinal_4282(panel, 0) / (.., 1): Extend) */
    if (g_qt_scripting) return;
    if (!pselDlg_) {
        pselDlg_ = new PlaneSelDialog(this); pselDlg_->hide();
        pselDlg_->withGL = [this](const std::function<void()> &fn) { if (g_mw) view_->withGL(fn); };
        connect(pselDlg_, &PlaneSelDialog::changed, this, [this] { refreshAll(); });
    }
    if (psel_open && !psel_cue) { bool f = !pselDlg_->isVisible(); pselDlg_->refresh(); if (f) { static bool placed = false; if (!placed) { placeRight(pselDlg_); placed = true; } pselDlg_->show(); } pselDlg_->setEnabled(!psel_named); }   /* the position kept from call to call (DestroyWindow FUN_180238590 saves the rect, OnInitDialog restores it); the first: the right-hand edge (ours) */
    else if (pselDlg_->isVisible()) pselDlg_->hide();
}
/* cui_hooks.selrun_dialog: the command's dialog for the selection (Change Levels / Attributes; centred - their position NOT DECODED) */
void MainWindow::openSelrun(int kind) {
    if (!clDlg_) return;
    FloatDlg *d = kind == SR_LEVEL ? (FloatDlg *)clDlg_ : (FloatDlg *)atDlg_;
    if (kind == SR_LEVEL) clDlg_->start(); else atDlg_->start();
    d->adjustSize(); d->move(view_->widget()->mapTo(this, QPoint((view_->widget()->width() - d->width()) / 2, (view_->widget()->height() - d->height()) / 2)));
    d->show(); d->raise();
}
/* Home > Copy Image (ToClipboard @1802732d0): get_window_corners (opmanres 3288; Esc = the whole graphics window), the screen bitmap
   cropped (CDib::ScreenToBitmap_SAVE / _CROP) to the clipboard (CopyBitmapToClipboard); a failure: opmanres 3289 (message_box_ok 0xcd9) */
void MainWindow::copyImageTest(const QString &arg) {
    QStringList v = arg.split(',');
    if (v.size() == 4) { copyImagePick_ = true; ciX_[0] = v[0].toInt(); ciY_[0] = v[1].toInt(); ciX_[1] = v[2].toInt(); ciY_[1] = v[3].toInt(); copyImage(false); }
    else { copyImagePick_ = true; copyImage(true); }
    QImage im = QApplication::clipboard()->image();
    printf("    clipboard image %dx%d\n", im.width(), im.height());
}
void MainWindow::copyImage(bool whole) {
    copyImagePick_ = false; cam_msg[0] = 0;
    QImage img = view_->grabView();
    if (img.isNull()) { message("apprenticeCAM", "Error processing bitmap image"); refreshAll(); return; }
    if (!whole) {
        double sx = (double)img.width() / qMax(1, view_->widget()->width()), sy = (double)img.height() / qMax(1, view_->widget()->height());
        QRect r = QRect(QPoint(qMin(ciX_[0], ciX_[1]), qMin(ciY_[0], ciY_[1])), QPoint(qMax(ciX_[0], ciX_[1]), qMax(ciY_[0], ciY_[1])));
        r = QRect((int)(r.x() * sx), (int)(r.y() * sy), (int)(r.width() * sx), (int)(r.height() * sy)).intersected(img.rect());
        if (r.width() < 1 || r.height() < 1) { message("apprenticeCAM", "Error processing bitmap image"); refreshAll(); return; }
        img = img.copy(r);
    }
    QApplication::clipboard()->setImage(img);
    refreshAll();
}
void MainWindow::refreshTree() { if (tpm_) tpm_->refresh(); }

void MainWindow::message(const QString &title, const QString &text) {
    msg_ = title + ": " + text;
    msgShown_ = true; msgT_ = title.toUtf8(); msgX_ = text.toUtf8();
    fprintf(stderr, "%s\n", msg_.toUtf8().constData());
    if (qEnvironmentVariableIsSet("MCAMUI_SCRIPT")) return;   /* scripted runs: no modal boxes */
    QMessageBox::information(this, "apprenticeCAM", text);    /* message box captions are the program name */
}

/* every action that would drop the current document asks first when it has unsaved changes (mcamui.c request) */
void MainWindow::request(int act, const QString &path) {
    if (g_regen_busy) return;                            /* as invoke: open / new / close / exit wait for OK's computation */
    if (doc_dirty && (act == ACT_NEW || act == ACT_OPEN_PATH || act == ACT_CLOSE || act == ACT_EXIT || act == ACT_OPEN)) {
        confirmAct_ = act; confirmPath_ = path;
        if (qEnvironmentVariableIsSet("MCAMUI_SCRIPT")) return;   /* scripted runs: the prompt stays open (answer:save|discard) */
        /* mccore save_on_exit (decomp/mccore_save_on_exit.c): MCMessageBox(string 7291 formatted with the part file name
           (its extension made .mcam, FUN_180602020), MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2, caption string 5175).
           Yes (6) -> RunFileSaveDlg, No -> carry on without saving; there is no Cancel. Text: mccoreres 7291 verbatim with
           the program name for "Mastercam" (caption 5175 likewise: intentional) - mcamui.c draw_modals */
        QString fn;
        if (F.path[0]) { QFileInfo fi(QString::fromUtf8(F.path)); fn = fi.path() + "/" + fi.completeBaseName() + ".mcam"; }
        QMessageBox::StandardButton b = QMessageBox::question(this, "apprenticeCAM", QString("apprenticeCAM file '%1' has changed. Save it?").arg(fn),
                                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        answerPrompt(b == QMessageBox::Yes ? 1 : 2);
        return;
    }
    doAction(act, path);
}
/* the prompt's answer (mcamui.c confirm_choose): Yes = the Save dialog, then the action; No = the action, unsaved */
void MainWindow::answerPrompt(int pick) {
    if (!confirmAct_) return;
    int act = confirmAct_; QString path = confirmPath_; confirmAct_ = 0;
    if (pick == 1) { afterSaveAct_ = act; afterSavePath_ = path; docSave(true); }   /* save_on_exit: Yes -> RunFileSaveDlg */
    else if (pick == 2) { doc_dirty = 0; doAction(act, path); }
}
void MainWindow::doAction(int act, const QString &path) {
    if (g_regen_busy) return;
    switch (act) {
    case ACT_CLOSE: closePart(); break;
    case ACT_NEW: newPart(); break;
    case ACT_OPEN: openDialog(); break;
    case ACT_OPEN_PATH: openPath(path); break;
    case ACT_EXIT: quitting_ = true; close(); break;
    }
}

void MainWindow::openPath(const QString &p) {
    if (p.isEmpty()) return;
    bool ok = false;
    view_->withGL([&] { ok = qc_open(p.toUtf8().constData()); });   /* core_ui open_part: PART.mcamui sidecar / .mcamui project */
    if (!ok) return;                                    /* the previous document is untouched (the message hook said why) */
    msg_.clear();
    machine_ = QString::fromUtf8(machine); updateContexts();   /* the project's MACHINE line */
    showBackstage(false);
    refreshAll();
}

void MainWindow::newPart() {
    view_->withGL([] { qc_new(); });
    msg_.clear();                                      /* File > New shows no message */
    machine_ = "Mill"; updateContexts();               /* init_db: machine group none -> Mill here (mcamui do_action ACT_NEW) */
    showBackstage(false);
    setTabByName("Wireframe");                         /* back to the graphics window, on the Wireframe tab */
    refreshAll();
}

void MainWindow::closePart() { view_->withGL([] { qc_close(); }); msg_.clear(); refreshAll(); }

/* the .mcamui project (mcamui.c doc_save / doc_save_to; core_ui cui_doc_save_target / cui_doc_save_to). The Save As
   dialog's types are mcamui's FDT_SAVE: the project, or a Mastercam part (export_mcam_dialog) */
void MainWindow::docSave(bool as) {
    char p[1200], dir[1100], name[256];
    int k = cui_doc_save_target(as, p, sizeof p, dir, sizeof dir, name, sizeof name);
    if (k < 0) { afterSaveAct_ = 0; return; }
    if (k > 0) { docSaveTo(QString::fromUtf8(p)); return; }
    QString sel = "apprenticeCAM Project (*.mcamui)";
    QString f = QFileDialog::getSaveFileName(this, "Save As", QString::fromUtf8(dir) + "/" + QString::fromUtf8(name),
                                             "apprenticeCAM Project (*.mcamui);;Mastercam 2017 Files (*.mcam *.mcx*);;All Files (*)", &sel);
    if (f.isEmpty()) { afterSaveAct_ = 0; return; }   /* Cancel: the waiting action is dropped (mcamui fd Cancel) */
    if (sel.startsWith("Mastercam")) { afterSaveAct_ = 0; savePartAs(f); return; }
    if (qc_is_part_file(f.toUtf8().constData())) { message("Save", "part files are never overwritten"); afterSaveAct_ = 0; return; }   /* mcamui fd_accept */
    docSaveTo(f);
}
bool MainWindow::docSaveTo(const QString &p) {
    if (!cui_doc_save_to(p.toUtf8().constData())) { afterSaveAct_ = 0; return false; }   /* saving is silent (item 17) */
    updateTitle(); backstage_->refresh();
    if (afterSaveAct_) { int a = afterSaveAct_; QString ap = afterSavePath_; afterSaveAct_ = 0; doAction(a, ap); }
    return true;
}

/* ------------------------------------------------------------------------------------------------ graphics view */
void MainWindow::redrawView() { syncGeo(); syncChain(); syncExtrude(); syncFillet(); syncOps(); syncTools(); syncMisc(); syncPlaneSel(); overlay_dirty = 1; view_->redraw(); if (ViewInput *in = viewInput(view_)) in->overlay()->update(); }

/* ------------------------------------------------------------------------------------------------ wireframe commands */
/* the panel dock follows core_ui's geo_cmd: a new command rebuilds the panel (its title = the command's name), the
   end of a command closes the tab (the manager tab shows again), otherwise the controls are refreshed */
void MainWindow::syncGeo() {
    if (!geo_ || !geoDock_) return;
    int pc = geo_cmd && geo_has_panel(geo_cmd) ? geo_cmd : 0;   /* a command without a function panel (Remove Boundary): none shown */
    if (pc != shownGeo_ || (pc && geo_panel_layout != shownLayout_)) {   /* a new command, or its panel's rows changed (geo_panel_layout) */
        shownGeo_ = pc; shownLayout_ = geo_panel_layout;
        if (pc) {
            geo_->rebuild();
            geoDock_->setWindowTitle(QString::fromUtf8(geo_name(geo_cmd)));
            if (geoDock_->isClosed()) { geoDock_->toggleView(true); }
            geoDock_->setAsCurrentTab();
        } else {
            geo_->rebuild();
            geoDock_->toggleView(false);
        }
    } else if (pc) geo_->refresh();
}
void MainWindow::geoStarted() {                         /* mcamui hook_geo_started: cam_mode / show_params off, cam_clear_selection, dock_show_panel = 1 */
    cam_mode = 0; show_params = 0; cam_clear_selection();
    if (geoDock_ && shownGeo_ && !geoDock_->isClosed()) geoDock_->setAsCurrentTab();
}
void MainWindow::geoButton(int code) {
    int cmd = geo_cmd;
    if (code == 1 && geo_cmd == G_XDYN) xdy_ctrl_held = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;   /* Dynamic's Ctrl+Apply (GetAsyncKeyState(0x11)) */
    view_->withGL([cmd, code] { if (geo_cmd == cmd) geo_panel_button(code); });
    refreshAll();
}
/* ------------------------------------------------------------------------------------------------ Extrude */
/* the panel follows core_ui's ex_mode && ex_phase (mcamui draw_extrude_panel): shown and brought to the front when it
   comes up, closed when the command ends or goes back to the chain selection (Add chains / Rechain all); while it is up
   every refresh runs the panel frame (ex_force, the preview when due, the arrows' check), except during a scripted step
   (mcamui runs its script before the first frame, so the preview never runs between steps there) */
void MainWindow::syncExtrude() {
    if (!ex_ || !exDock_) return;
    bool up = ex_mode && ex_phase;
    if (up != shownEx_) {
        shownEx_ = up;
        if (up) { if (exDock_->isClosed()) exDock_->toggleView(true); exDock_->setAsCurrentTab(); }
        else exDock_->toggleView(false);
    }
    if (!up) return;
    if (!g_qt_scripting) view_->withGL([] { ex_panel_frame(); });
    ex_->refresh();
}
/* Constant Fillet (core_ui cui_fillet.c): the dock is up while the command runs - the Solid Selection page during the pick, the panel after;
   the preview runs after every change (never during a scripted step, as the Extrude panel) */
void MainWindow::syncFillet() {
    if (!fl_ || !flDock_) return;
    bool up = fl_mode != 0;
    if (up != shownFl_) {
        shownFl_ = up;
        if (up) { if (flDock_->isClosed()) flDock_->toggleView(true); flDock_->setAsCurrentTab(); }
        else flDock_->toggleView(false);
    }
    if (!up) return;
    if (!g_qt_scripting) view_->withGL([] { fl_panel_frame(); });
    fl_->refresh();
}
void MainWindow::flButton(int code) {                    /* 0 OK (0x2bd), 1 OK-New / OK-Regen (0x2c0), 2 Cancel (0x2be), 3 the Solid Selection's OK */
    view_->withGL([code] { if (!fl_mode) return; if (code == 3) fl_sel_done(); else if (code == 2) fl_cancel(); else fl_accept(code); });
    refreshAll();
}
void MainWindow::exButton(int code) {                    /* 0x2bd OK / 0x2c0 OK-New (Apply) or OK-Regen / 0x2be Cancel */
    view_->withGL([code] { if (!(ex_mode && ex_phase)) return; if (code == 0) ex_accept(0); else if (code == 1) ex_accept(1); else { ex_log("panel Cancel pressed"); ex_cancel(); } });
    refreshAll();
}
void MainWindow::showAcSettings() {
    auto *d = new AcSettingsDialog(this); d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &QDialog::accepted, this, [this] { redrawView(); });
    d->open();                                         /* window-modal, returns at once (the test runs grab it with the window) */
}

/* a left click in the view (mcamui.c handle_click, the cases this UI has): Alt+F12's rotation centre pick, else the
   entity under the cursor flips its selection (select_ent_univ: the nearest within the aperture) */
void MainWindow::viewClick(float x, float y) {
    geo_snap_ptr(x, y);                                  /* every snap of this click searches around the click (core_ui cui_snap.c) */
    viewClickIn(x, y);
    geo_snap_ptr_off();
}
void MainWindow::viewClickIn(float x, float y) {
    if (rotposPick_) {                                   /* Alt+F12: the clicked point becomes the rotation centre */
        double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x, y - vp_y, w);
        int e2 = pick_entity(x, y);                      /* on an entity: its nearest end / centre */
        if (e2 >= 0) { V3 p = { w[0], w[1], w[2] }; if (geo_snap(&p, NULL, -1, 0)) { w[0] = p.x; w[1] = p.y; w[2] = p.z; } }
        scene_set_center(w[0], w[1], w[2]); rotposPick_ = false;
        cam_msg[0] = 0; redrawView(); return;
    }
    /* get_plane_info's get_event (core_ui cui_plsel.c): it runs inside the command that called it, every click is its event */
    if (psel_open) { double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x, y - vp_y, w); V3 p = { w[0], w[1], w[2] }; int sn = geo_snap(&p, NULL, -1, 0) != 0;
                     double ws[3] = { p.x, p.y, p.z }; view_->withGL([&] { psel_click(x, y, w, ws, sn); }); refreshAll(); return; }
    /* the dialogs' picks (mcamui handle_click order): Colors > Select (2798), Levels > select entity (214), Analyze's AutoCursor
       (5342, AutoCursor-snapped), then the Linking page, the analyze pick (6333) */
    if (ns_deleting) { int e2 = pick_entity(x, y); view_->withGL([e2] { ns_del_pick(e2); }); refreshAll(); return; }   /* Nesting Results Delete: get_event event 2 (core_ui cui_nest.c) */
    if (srf_cmd) { view_->withGL([x, y] { srf_click(x, y); }); refreshAll(); return; }   /* a Surfaces command's pick (core_ui cui_surf.c) */
    if (cdlg_picking) { color_dialog_pick_ent(pick_entity(x, y)); refreshAll(); return; }
    if (ldlg_picking) { level_dialog_pick_ent(pick_entity(x, y)); refreshAll(); return; }
    if (az_pick) { double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x, y - vp_y, w); V3 p = { w[0], w[1], w[2] }; geo_snap(&p, NULL, -1, 0);
                   view_->withGL([&] { az_autocursor_pick(p.x, p.y, p.z); }); refreshAll(); return; }
    /* Linking page "..." (HSTLinkPageBase::OnDepthPb & co -> mccore tp_select_depth 0x1800cd910, prompt mccoreres 5321
       "Select point"): the field gets the picked point's Z (core_ui lk_click: AutoCursor; a miss = the construction depth 0) */
    /* Contour Tabs Position... (core_ui tb_pick): the chain entity, then the tab's place on it */
    if (tb_pick == 1) { tb_click_ent(pick_entity(x, y)); syncOps(); refreshAll(); return; }
    if (tb_pick == 2) { double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x, y - vp_y, w); tb_click_pt(w[0], w[1]); refreshAll(); return; }
    if (lk_pick) { double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x, y - vp_y, w); lk_click(w[0], w[1], grid_z()); refreshAll(); return; }   /* a miss = the construction depth, as mcamui */
    if (st_point_pick()) { double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x, y - vp_y, w); st_pick_click(w[0], w[1], grid_z()); refreshAll(); return; }   /* Stock Setup picks (core_ui cui_stock.c) */
    if (st_pick == ST_PICK_SOLID) { st_solid_click(x, y); refreshAll(); return; }   /* Stock Setup 528: the solid pick (core_ui cui_stock.c) */
    if (drv_pick && !show_params) { view_->withGL([x, y] { drv_click(x, y); }); refreshAll(); return; }   /* a 3D op's drive / check prompt (core_ui cui_chain.c drv_click) */
    if (copyImagePick_) {                                 /* Copy Image: get_window_corners - two corners of the area (ToClipboard @1802732d0) */
        ciX_[ciN_] = (int)x; ciY_[ciN_] = (int)y; if (++ciN_ == 2) copyImage(false); return; }
    if (zd_picking()) { double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x, y - vp_y, w); V3 p = { w[0], w[1], w[2] }; geo_snap(&p, NULL, -1, 0);   /* Z depth: the AutoCursor point (3D) */
                        view_->withGL([&] { zd_click(p.x, p.y, p.z); }); syncHome(); refreshAll(); return; }
    if (plg_picking()) { double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x, y - vp_y, w); V3 p = { w[0], w[1], w[2] }; geo_snap(&p, NULL, -1, 0);   /* a 3D op's point picks after its dialog (core_ui plg_click) */
                         view_->withGL([&] { plg_click(p.x, p.y, p.z); }); refreshAll(); syncOps(); return; }
    if (analyze_pick) { int e2 = pick_entity(x, y); view_->withGL([e2] { analyze_pick_ent(e2); }); refreshAll(); return; }   /* core_ui cui_analyze.c */
    if (fl_mode) {                                        /* Constant Fillet: the pick (sld_array_pick, Shift = tangents; core_ui fl_click); the panel takes no pick */
        bool sh = (QApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
        if (fl_phase == 0) view_->withGL([&] { fl_click(x, y, sh); });
        refreshAll(); return;
    }
    if (ex_mode) { int pe_ = pick_entity(x, y); ex_log("graphics click at %.0f,%.0f (%s), %d chain(s) so far, entity under the cursor: %s%d", x, y, ex_phase ? "panel up" : "chaining", ncchains, pe_ < 0 ? "none " : "", pe_); }
    if (ex_mode && ex_phase) {                            /* the Extrude panel's graphics events (formats/extrude.md 1.3; core_ui ex_click) */
        double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x, y - vp_y, w);
        view_->withGL([&] { ex_click(x, y, w); });
        refreshAll(); return;
    }
    bool taken = false;                                  /* Area, Polygon / Vector points, the window start point, Point mode (core_ui) */
    view_->withGL([&] { taken = chain_click_pre(x, y) != 0; });
    if (taken) { refreshAll(); return; }
    if (srf_solid_selecting()) { view_->withGL([&] { double w0[3] = { 0, 0, 0 }; geo_click(x, y, w0, 0); }); refreshAll(); return; }   /* From Solid's body pick (core_ui cui_geo.c) */
    if (srf_ned_clicks()) { view_->withGL([&] { double w0[3] = { 0, 0, 0 }; geo_click(x, y, w0, 0); }); refreshAll(); return; }   /* Normals > Set / Untrim: surface picks (core_ui cui_srfedit.c) */
    if (geo_cmd && geo_need()) {                         /* drawing: a point (AutoCursor) or an entity (core_ui geo_click) */
        double w[3]; scene_pick(vp_x, 0, vp_w, vp_h, x, y - vp_y, w);
        bool ctrl = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
        view_->withGL([&] { geo_click(x, y, w, ctrl); });
        refreshAll(); return;
    }
    int psol = -1, e = pick_entity_gs(x, y, &psol);     /* the general selection: an entity, else a part solid (core_ui cui_sel.c) */
    /* a double-click with no command running: the properties dialog of the entity / part solid under the cursor (core_ui
       analyze_dblclick; user report 2026-10-24) */
    if (last_clicks >= 2 && !cam_mode && !geo_cmd && !show_params && !drv_pick && !st_pick) {
        int sol = e < 0 ? (psol >= 0 ? psol : st_pick_solid_at(x, y)) : -1;
        if (e >= 0 || sol >= 0) { view_->withGL([e, sol] { analyze_dblclick(e, sol); }); refreshAll(); return; }
    }
    if (e < 0 && psol >= 0) { sld_sel_toggle(psol); geo_applied = 0; redrawView(); return; }   /* a solid: its selection bit 4 flips (FUN_1804e8d50) */
    if (e < 0) { view_->withGL([] { pick_noport_msg(); }); return; }   /* CS-20: shaded picks not decoded say so */
    view_->withGL([&] { taken = chain_click_ent(e, x, y) != 0; });   /* drill points / run_chain_dlg picks (core_ui) */
    if (taken) { redrawView(); return; }
    sel_toggle(e); geo_applied = 0;                     /* a new selection: a selection command's OK may apply again */
    redrawView();
}

/* ------------------------------------------------------------------------------------------------ chaining */
/* the dialogs follow core_ui's chaining state as mcamui's draw_cam does every frame: the Chaining dialog (6464 / 1058)
   while toolpath chaining (cam_mode) or the Extrude command's chain selection runs and the op dialog is not up, the Drill
   Point Selection (2300) instead for the point ops, Chaining Options (405) while show_chain_opts; the prompt follows the
   chain count. Position: the top right of the graphics area (mcamui: W - width - 30, TOP_H + 10; ours = its place). */
/* ours (user request 2026-10-24, formats/user_decisions.md UI-1): every dialog opens at the program's right-hand edge, just
   under the ribbon, and is kept on the screen; the user can still drag it. Not from Mastercam (which remembers each
   dialog's last position); the Simulator window keeps Mastercam's own UbvsPersistant position (simwin.cpp). */
void MainWindow::placeRight(QWidget *d) {
    d->ensurePolished();
    QSize s = d->testAttribute(Qt::WA_Resized) || d->isVisible() ? d->frameSize() : d->sizeHint();
    QRect fr = frameGeometry();
    int x = fr.right() - s.width() - 8, y = mapToGlobal(QPoint(0, centralWidget()->y())).y() + 10;
    if (QScreen *sc = screen()) {
        QRect a = sc->availableGeometry().united(fr);   /* the window itself counts (the offscreen test screen is smaller than the window) */
        x = qBound(a.left(), x, qMax(a.left(), a.right() - s.width()));
        y = qBound(a.top(), y, qMax(a.top(), a.bottom() - s.height()));
    }
    d->move(x, y);
}

void MainWindow::syncChain() {
    bool shown = chain_dlg_shown() != 0, pts = shown && chain_points_dlg();
    if (!chainDlg_) {
        chainDlg_ = new ChainDialog(this);
        connect(chainDlg_, &ChainDialog::act, this, [this](int id) { chainAct(id); });
        connect(chainDlg_, &FloatDlg::closeClicked, this, [this] { chainClose(); });
        drillDlg_ = new DrillDialog(this);
        connect(drillDlg_, &DrillDialog::changed, this, [this] { redrawView(); });
        connect(drillDlg_, &DrillDialog::ok, this, [this] { view_->withGL([] { drill_ok(); }); refreshAll(); });
        connect(drillDlg_, &FloatDlg::closeClicked, this, [this] { chainClose(); });
    }
    auto place = [this](QWidget *d) { placeRight(d); };
    if (!refDlg_) {                                      /* mcgeomsldres 134 Pick Reference Face (core_ui sld_ref_pending) */
        refDlg_ = new RefFaceDialog(this);
        connect(refDlg_, &RefFaceDialog::act, this, [this](int id) { view_->withGL([id] { if (id == 273) sld_ref_other(); else if (id == 1) sld_ref_ok(); }); refreshAll(); });
    }
    bool ref = shown && !pts && sld_ref_pending();       /* FUN_1800a0dd0: show_chain_dlg('\0') while it is up */
    if (ref) { if (!refDlg_->isVisible()) { place(refDlg_); refDlg_->show(); } }
    else if (refDlg_->isVisible()) refDlg_->hide();
    if (shown && !pts && !ref) {
        if (!chainDlg_->isVisible() || chain_win_new) { chainDlg_->refresh(); place(chainDlg_); chainDlg_->show(); chain_win_new = 0; }
        else chainDlg_->refresh();
    } else if (chainDlg_->isVisible()) chainDlg_->hide();
    if (pts) {
        drillDlg_->refresh();
        if (!drillDlg_->isVisible() || chain_win_new) { place(drillDlg_); drillDlg_->show(); chain_win_new = 0; }
    } else if (drillDlg_->isVisible()) drillDlg_->hide();
    if (shown && chainLastN_ != ncchains) { chain_prompt_upd(); chainLastN_ = ncchains; }   /* mcamui: last_n != ncchains */
    if (!shown) chainLastN_ = -1;
    if (shown && show_chain_opts && !pts) {              /* Chaining Options (dialog 405): to the left of the Chaining dialog (mcamui W - 2 x width - 40) */
        if (!optsDlg_) {
            optsDlg_ = new ChainOptsDialog(this); optsDlg_->setAttribute(Qt::WA_DeleteOnClose);
            connect(optsDlg_, &QObject::destroyed, this, [this] { optsDlg_ = nullptr; });
            connect(optsDlg_, &QDialog::finished, this, [this] { redrawView(); });
            optsDlg_->move(chainDlg_->x() - optsDlg_->width() - 10, chainDlg_->y());
            optsDlg_->open();
        }
    } else if (optsDlg_) { optsDlg_->close(); }
}
void MainWindow::chainAct(int id) {
    view_->withGL([id] { chain_act(id); });              /* Point mode / Last / OK may change the part or the op state */
    refreshAll();
}
/* the chaining's OK (core_ui chain_act(1) / drill_ok): mcamui hook_chain_ok = the Extrude command's ex_chains_done (phase
   5), else open_params - the op dialog with the pending op (phase 6). Until those phases land: the chains are kept for
   Last (open_params' first step, core_ui chain_keep_last) and the op dialog state is entered (show_params, as
   open_params) with no dialog to show yet; Esc leaves it as mcamui's Esc leaves the op dialog. */
void MainWindow::chainOk() {
    if (geo_chaining) { geo_chains_done(); return; }     /* a wireframe chain command's chain_it (core_ui geo_chains_done, mcamui hook_chain_ok) */
    if (ex_mode) { ex_chains_done(); return; }           /* the Extrude command's chains (core_ui ex_chains_done, mcamui hook_chain_ok) */
    open_params();                                       /* the op dialog with the pending op (core_ui cui_ops.c, mcamui hook_chain_ok) */
    syncOps();
}
/* ------------------------------------------------------------------------------------------------ the toolpath dialog */
/* the op dialog (6544) while show_params and the Tool List (6410) while tl6410_open, as mcamui draws them every frame
   (draw_op_params / draw_tool_list_6410). Not while a script step runs: the pages' own writes (lk_frame, the combos'
   assignments) happen when they are drawn, and mcamui draws nothing before its script has run. Position: mcamui puts
   the dialog at the top right of the window (W - width - 20, TOP_H + 10), here the top right of the graphics area; the
   Tool List centred on the window. */
void MainWindow::syncOps() {
    if (g_qt_scripting) return;
    if (!opDlg_) {
        opDlg_ = new OpDialog(this);
        opDlg_->withGL = [this](const std::function<void()> &fn) { view_->withGL(fn); };
        connect(opDlg_, &OpDialog::changed, this, [this] { syncOps(); syncTools(); });   /* menu 1247 / the toolbar may open the wizard / Tool Manager */
        connect(opDlg_, &OpDialog::done, this, [this] { refreshAll(); });
        tlDlg_ = new ToolListDialog(this);
        connect(tlDlg_, &ToolListDialog::closed, this, [this] { syncOps(); });
        plsDlg_ = new PlanesDialog(this, 0); uplDlg_ = new PlanesDialog(this, 1);
        for (PlanesDialog *d : { plsDlg_, uplDlg_ }) connect(d, &PlanesDialog::closed, this, [this] { opDlg_->refresh(); syncOps(); syncMisc(); overlay_dirty = 1; view_->redraw(); });
    }
    /* a Linking page "..." pick: the dialog is suspended while tp_select_depth runs and comes back after it
       (HSTLinkPageBase handlers FUN_180019090 / FUN_180019160, decomp/mcmill_all.c:18975-19020: AfxSuspendWindow(dialog,
       0, 6) -> tp_select_depth -> AfxResumeWindow(0, 9)); it keeps its position */
    /* batch 12: Hide dialog 10320 (core_ui pp_hide: OnHideDialogPb's AfxSuspendWindow until the prompt's Esc) - suspended the same way */
    /* Contour Tabs Position... (core_ui tb_pick: FUN_180190070's AfxSuspendWindow(.., 0, 6) until the command ends, AfxResumeWindow(0, 9)) */
    if (show_params && (lk_pick || pp_hide_on || tb_pick)) { if (opDlg_->isVisible()) { opDlg_->hide(); opSuspended_ = true; } }
    else if (show_params) {
        bool fresh = !opDlg_->isVisible() && !opSuspended_;
        if (fresh || opDlg_->stale() || opSuspended_) opDlg_->refresh();
        if (fresh) placeRight(opDlg_);
        if (!opDlg_->isVisible()) opDlg_->show();
        opSuspended_ = false;
    } else { if (opDlg_->isVisible()) opDlg_->hide(); opSuspended_ = false; }
    if (show_params && tl6410_open) {
        tlDlg_->refresh();
        if (!tlDlg_->isVisible()) { placeRight(tlDlg_); tlDlg_->show(); tlDlg_->raise(); }
    } else if (tlDlg_->isVisible()) tlDlg_->hide();
    /* the Planes page's dialogs (mcamui opparams_ui.inc draw_planes_dialogs), centred on the window */
    for (int u = 0; u < 2; u++) {
        PlanesDialog *d = u ? uplDlg_ : plsDlg_; bool on = (show_params || (!u && show_stock_setup && pls_which == 3) || (!u && pls_which == 4)) && (u ? upl_open : pls_open);   /* 4: get_plane_info's Named */   /* + the Stock Setup's stock plane (960) */
        if (on) { d->refresh(); if (!d->isVisible()) { placeRight(d); d->show(); } d->raise(); }
        else if (d->isVisible()) d->hide();
    }
}
/* phase 7: the tool dialogs follow the model (mcamui draws them every frame: draw_tool_manager / draw_tool_filter /
   draw_tool_editor / draw_coolant_dialog / draw_tool_report / draw_renumber); centred on the window as mcamui's */
void MainWindow::syncTools() {
    if (g_qt_scripting) return;
    if (!tmDlg_) {
        tmDlg_ = new ToolManagerDialog(this); tfDlg_ = new ToolFilterDialog(this); tlrWin_ = new ReportWindow(this);
        rnDlg_ = new RenumberDialog(this); twDlg_ = new ToolWizardDialog(this); coolDlg_ = new CoolantDialog(this);
        rnDlg_->withGL = [this](const std::function<void()> &fn) { view_->withGL(fn); };
        auto all = [this] { syncTools(); syncOps(); tpm_->refresh(); updateTitle(); };
        connect(tmDlg_, &ToolManagerDialog::changed, this, all);
        connect(tfDlg_, &ToolFilterDialog::closed, this, all);
        connect(twDlg_, &ToolWizardDialog::changed, this, all);
        connect(coolDlg_, &CoolantDialog::closed, this, all);
        connect(rnDlg_, &RenumberDialog::closed, this, [this] { refreshAll(); });   /* RefreshAll (opman_tree 22145) */
    }
    auto centre = [this](QWidget *d) { placeRight(d); };   /* ours: the right-hand edge (placeRight), no longer centred */
    if (show_tm) {
        bool fresh = !tmDlg_->isVisible();
        if (fresh) tm_open_init();                     /* OnInitDialog (core_ui: settings, filter defaults, the library to open) */
        tmDlg_->refresh(fresh);
        if (fresh) { centre(tmDlg_); tmDlg_->show(); }
    } else if (tmDlg_->isVisible()) tmDlg_->hide();
    if ((show_tm || show_params) && tmf_open) {   /* batch 12: also the Tool page's Filter... (core_ui tp_filt_open) */ bool fresh = !tfDlg_->isVisible(); if (fresh) { tfDlg_->refresh(); centre(tfDlg_); tfDlg_->show(); } tfDlg_->raise(); }
    else if (tfDlg_->isVisible()) tfDlg_->hide();
    if (tedit_index > -2) { bool fresh = !twDlg_->isVisible(); twDlg_->refresh(fresh); if (fresh) { centre(twDlg_); twDlg_->show(); } twDlg_->raise(); }
    else if (twDlg_->isVisible()) twDlg_->hide();
    if (cool.open) { coolDlg_->refresh(); if (!coolDlg_->isVisible()) { centre(coolDlg_); coolDlg_->show(); } coolDlg_->raise(); }
    else if (coolDlg_->isVisible()) coolDlg_->hide();
    if (show_tlr) { if (!tlrWin_->isVisible() || tlr_new != tlrShown_) { tlrWin_->refresh(); tlrShown_ = tlr_new; tlr_new = 0; } if (!tlrWin_->isVisible()) { centre(tlrWin_); tlrWin_->show(); } }
    else if (tlrWin_->isVisible()) tlrWin_->hide();
    if (show_rn) { if (!rnDlg_->isVisible()) { rnDlg_->refresh(); centre(rnDlg_); rnDlg_->show(); } else rnDlg_->refresh(); }
    else if (rnDlg_->isVisible()) rnDlg_->hide();
}
/* the dialog is modal (DoModal): while the Tool Manager (or the wizard over it) is up every key goes to it, none to the
   graphics window's keymap (mcamui tm_keydown; the wizard: Esc = Cancel, the coolant dialog first) */
bool MainWindow::toolKey(QKeyEvent *k) {
    if (!show_tm && tedit_index < -1 && !cool.open) return false;
    QWidget *fw = QApplication::focusWidget();
    bool text = qobject_cast<QLineEdit *>(fw) || qobject_cast<QTextEdit *>(fw) || qobject_cast<QPlainTextEdit *>(fw) || qobject_cast<QAbstractSpinBox *>(fw);
    if (k->key() == Qt::Key_Escape) {
        if (cool.open) cool_cancel(); else if (tedit_index > -2) tedit_close();   /* mcamui: CoolantView IsCancel, the wizard */
        else if (tmDlg_) tmDlg_->keyDown(Qt::Key_Escape, false); else tm_escape();
        syncTools(); syncOps(); return true;
    }
    if (text) return false;                            /* the dialogs' own fields */
    if (tedit_index > -2 || cool.open) return show_tm != 0;   /* over the Tool Manager: no key reaches the keymap (mcamui: handled = show_tm) */
    if (tmDlg_) tmDlg_->keyDown(k->key(), (k->modifiers() & Qt::ControlModifier) != 0);
    return true;
}
/* a file dropped on the window (mcamui SDL_DROPFILE): a tool library opens in the Tool Manager, anything else as a part */
void MainWindow::dragEnterEvent(QDragEnterEvent *e) { if (g_regen_busy) { e->ignore(); return; } if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void MainWindow::dropEvent(QDropEvent *e) {
    if (g_regen_busy) { e->ignore(); return; }           /* a dropped part would replace F while OK's computation reads it */
    for (const QUrl &u : e->mimeData()->urls()) {
        QString p = u.toLocalFile(); if (p.isEmpty()) continue;
        if (p.endsWith(".tooldb", Qt::CaseInsensitive) || p.endsWith(".tools", Qt::CaseInsensitive)) { tm_open_lib(p.toUtf8().constData()); show_tm = 1; syncTools(); }
        else request(ACT_OPEN_PATH, p);
        break;
    }
}
void MainWindow::openOpParams(int op) { open_op_params(op); refreshAll(); }   /* the tree's Parameters row / a double click (core_ui open_op_params) */
void MainWindow::opMenuRun(int item) { view_->withGL([item] { cm_run(item); }); refreshAll(); syncMisc(); }   /* the Toolpaths manager menu (core_ui cm_run) */
void MainWindow::treeExpand(bool on) { if (tpm_) tpm_->expandAll(on); }
/* Unselect All (FUN_1801ee0f0 -> run_chain_dlg msg 0xb): MCMessageBox(5007 "Are you sure?", MB_YESNO, caption 10206
   "Unselect All Chains?"); Yes -> FUN_1801eff00. MB_YESNO without DEFBUTTON2: the default is Yes (mcamui draw_modals).
   Scripted runs: no modal box (the step chainyes answers) */
void MainWindow::chainAskUnselectAll() {
    if (qEnvironmentVariableIsSet("MCAMUI_SCRIPT")) { printf("    (Unselect All Chains? asked)\n"); return; }
    if (QMessageBox::question(this, "Unselect All Chains?", "Are you sure?", QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) chain_unselect_all();
}
bool MainWindow::galleryPick(const QString &name) {
    bool ok = false; QByteArray n = name.toUtf8();
    view_->withGL([&] { ok = gallery_pick(n.constData()) != 0; });
    refreshAll();
    return ok;
}
void MainWindow::chainClose() {                          /* the close box: chain_dlg_done (Chaining only), then cancel_chaining */
    if (!chain_points_dlg()) chain_dlg_done();
    if (geo_chaining) view_->withGL([] { geo_cancel(); });   /* chain_it returns 2: the chain command ends (core_ui geo_end) */
    else if (ex_mode) view_->withGL([] { ex_escape(); }); else chain_cancel();
    refreshAll();
}

/* the mini toolbar shown with the graphics right-click menu (mastercam.dll FUN_1801c9660 -> UIMiniToolbar with the
   ribbon controller's list, ribbonui 0x18007a360): ribbon commands 53409 Point Style, 53410 Line Style, 53411 Line
   Width (combos, widths 39 / 59 / 59), 53412 Wireframe / 53414 Solid / 53416 Surface Color, 54203 Set All, 53419 Set
   From Entity, 53418 Clear Colors, 53420 CPlane 2D/3D, 53421 Select Z Depth, 53423 Change Level - images = their
   INDEX_SMALL in IDR_BCGP_IMAGE_CATEGORY_SMALL. Set From Entity and Clear Colors are done (point / line style, width and the colour
   commands: core_ui cui_attr.c, 2026-10-28); its position relative to the menu is NOT DECODED (above it, as mcamui:
   gm_pos.y - 36, width 8 + the buttons, combos x 1.5, icon buttons 24, 2 px apart). mcamui.c draw_mini_toolbar */
static const struct { int combo_w, img; const char *tip, *fn; } MINI_TB[] = {
    { 39, -1, "Point Style", "ChangeSelectedEntityPointStyle" }, { 59, -1, "Line Style", "ChangeSelectedEntityLineStyle" }, { 59, -1, "Line Width", "ChangeSelectedEntityLineWidth" },
    { 0, 82, "Wireframe Color", "ChangeSelectedWireframeColorWithPicker" }, { 0, 84, "Solid Color", "ChangeSelectedSolidsColorWithPicker" }, { 0, 85, "Surface Color", "ChangeSelectedSurfacesColorWithPicker" },
    { 0, 120, "Set All", "ChangeEntityAttribs" }, { 0, 87, "Set From Entity", "set_main_from_entity" }, { 0, 86, "Clear Colors", "ClearColors" },
    { 0, 88, "CPlane 2D/3D", "OnScreen2D3DToggle" }, { 0, 89, "Select Z Depth", "OnScreenZDepth" }, { 0, 90, "Change Level", "ChangeEntityLevel" } };   /* the combos: a click applies the main value (FT with -1, ours: no drop list here) */
class MiniToolbar : public QWidget {
public:
    explicit MiniToolbar(QWidget *parent) : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint) {
        setObjectName("miniToolbar");
        int x = 4;
        for (const auto &m : MINI_TB) { int bw = m.combo_w ? (int)(m.combo_w * 1.5f) : 24; rects_ << QRect(x, 5, bw, 22); x += bw + 2; }
        resize(x + 4, 32);
    }
    int hit(QPoint local) const { for (int i = 0; i < rects_.size(); i++) if (rects_[i].contains(local)) return i; return -1; }
    int hover = -1;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), palette().window()); p.setPen(QColor(160, 160, 160)); p.drawRect(rect().adjusted(0, 0, -1, -1));
        for (int i = 0; i < rects_.size(); i++) {
            const auto &m = MINI_TB[i]; QRect b = rects_[i]; bool live = m.fn != nullptr;
            if (live && hover == i) p.fillRect(b, QColor(205, 225, 245));   /* mcamui's hover fill */
            if (m.combo_w) { p.setPen(QColor(200, 200, 200)); p.drawRect(b.adjusted(0, 0, -1, -1)); continue; }
            QIcon ic = Icons::icon(QString("#file:IDR_BCGP_IMAGE_CATEGORY_SMALL_0_%1.png").arg(m.img), 16, 16);
            p.drawPixmap(b.x() + 4, b.y() + 3, ic.pixmap(16, 16, live ? QIcon::Normal : QIcon::Disabled));
        }
    }
private:
    QVector<QRect> rects_;
};

/* the graphics-window right-click menu: the "Right mouse button menu" dropdown of CONFIG/default.mtb (FUNCTION /
   SEPARATOR / DDCOMMAND = a submenu), item text = the command's menu string (cmd_labels.tsv); unported functions
   greyed (as the QAT); with the mini toolbar above it. Not while a wireframe command runs (mcamui: !geo_cmd).
   mcamui.c draw_graphics_menu */
void MainWindow::viewMenu(const QPoint &global) {
    if (rmb_main < 0 || geo_cmd) return;
    QMenu *mp = new QMenu(this); mp->setAttribute(Qt::WA_DeleteOnClose);
    MiniToolbar *mini = new MiniToolbar(this);
    mini->move(global.x(), global.y() - 36);
    mini->show();
    connect(mp, &QMenu::aboutToHide, mini, &QObject::deleteLater);
    /* the menu grabs the mouse: its clicks / moves over the mini toolbar are handed to it (tooltip + the live button) */
    struct MiniFilter : QObject {
        MiniToolbar *mt; MainWindow *mw; QMenu *menu;
        MiniFilter(MiniToolbar *t, MainWindow *w, QMenu *m) : QObject(m), mt(t), mw(w), menu(m) {}
        bool eventFilter(QObject *, QEvent *e) override {
            if (e->type() != QEvent::MouseMove && e->type() != QEvent::MouseButtonPress && e->type() != QEvent::MouseButtonRelease) return false;
            auto *me = static_cast<QMouseEvent *>(e); QPoint g = me->globalPosition().toPoint();
            if (!mt || !mt->isVisible() || !mt->geometry().contains(g)) { if (mt && mt->hover >= 0) { mt->hover = -1; mt->update(); } return false; }
            int i = mt->hit(mt->mapFromGlobal(g));
            if (e->type() == QEvent::MouseMove) { if (i != mt->hover) { mt->hover = i; mt->update(); if (i >= 0) QToolTip::showText(g, MINI_TB[i].tip, mt); } return true; }
            if (e->type() == QEvent::MouseButtonRelease && i >= 0 && MINI_TB[i].fn) { QString fn = MINI_TB[i].fn; menu->close(); mw->mcFunction(fn, false); }
            return true;
        }
    };
    mp->installEventFilter(new MiniFilter(mini, this, mp));
    QMenu &m = *mp;
    std::function<void(QMenu *, int, int)> fill = [&](QMenu *menu, int di, int depth) {
        const RmbDrop *d = &rmb_drop[di];
        for (int i = 0; i < d->n; i++) {
            const RmbItem &it = d->it[i];
            if (it.kind == 1) { menu->addSeparator(); continue; }
            if (it.kind == 2) {
                int k = rmb_find(it.sub);
                QMenu *sub = menu->addMenu(k >= 0 ? QString::fromUtf8(rmb_drop[k].name) : QString("?"));
                if (k >= 0 && depth == 0) fill(sub, k, 1);   /* mcamui gm_level opens one submenu level */
                continue;
            }
            QString fn = QString::fromUtf8(it.fn);
            QAction *a = menu->addAction(cmdLabel(fn));       /* text only (mcamui gm_level draws no images) */
            a->setEnabled(mcFunction(fn, true));
            connect(a, &QAction::triggered, this, [this, fn] { mcFunction(fn, false); });
        }
    };
    fill(&m, rmb_main, 0);
    m.popup(global);                                   /* non-blocking (the test runs grab it with the window) */
}

/* Mastercam's keymap (core_ui cui_kmap: the user's mastercam.kmp, else the shipped defaults): a bound key runs its FT
   function when this UI has it (mcamui.c kmap_dispatch); not while a text field has the keyboard, not on key repeat */
bool MainWindow::keymap(QKeyEvent *e) {
    if (e->isAutoRepeat()) return false;
    QWidget *fw = QApplication::focusWidget();
    if (qobject_cast<QLineEdit *>(fw) || qobject_cast<QTextEdit *>(fw) || qobject_cast<QPlainTextEdit *>(fw) || qobject_cast<QAbstractSpinBox *>(fw)) return false;
    Qt::KeyboardModifiers m = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
    for (int i = 0; i < cui_nkmap; i++) {
        QKeySequence ks = QKeySequence::fromString(QString::fromUtf8(cui_kmap[i].keys));
        if (ks.isEmpty()) continue;
        QKeyCombination kc = ks[0];
        if (kc.key() == e->key() && kc.keyboardModifiers() == m) {
            QString fn = QString::fromUtf8(cui_kmap[i].fn);
            if (!mcFunction(fn, true)) return false;     /* bound, but not ported here: the key does nothing (item 17) */
            mcFunction(fn, false); return true;
        }
    }
    return false;
}

bool MainWindow::savePartAs(const QString &p0) {
    QString p = p0;
    /* a 2017 part (qc_save_ext ".mcam": a new part / a 2017 source) under a .mcx-9 name: warn and offer the .mcam name, the save
       still allowed - user decision 2026-11-19 (formats/user_decisions.md); the dialog and its text are ours (no Mastercam source).
       Scripted: no dialog, the save goes on under the given name (mcam_save_part's message carries the warning) */
    if (!g_qt_scripting && qc_ext_mismatch(p.toUtf8().constData())) {
        QString alt = p.left(p.size() - 6) + ".mcam";
        QMessageBox mb(QMessageBox::Warning, "Save as MCAM part", QString::fromUtf8(qc_ext_warning()), QMessageBox::NoButton, this);
        QPushButton *useMcam = mb.addButton("Save as " + QFileInfo(alt).fileName(), QMessageBox::AcceptRole);
        QPushButton *keep = mb.addButton("Save as " + QFileInfo(p).fileName(), QMessageBox::DestructiveRole);
        mb.addButton(QMessageBox::Cancel);
        mb.setDefaultButton(useMcam);
        mb.exec();
        if (mb.clickedButton() == useMcam) p = alt;
        else if (mb.clickedButton() != keep) return false;
    }
    char msg[1400] = "";
    bool ok = qc_save_part(p.toUtf8().constData(), msg, sizeof msg);
    message(ok ? "Saved as MCAM part" : "Save as MCAM part", QString::fromUtf8(msg));
    backstage_->refresh();
    return ok;
}

/* file types (the OPENFILENAME filters of the Windows dialogs; decomp/mastercam_filedlg.c): Open (mccore OpenFile,
   title mccoreres 7283 "Open", filter FUN_1803552e0: 2860 "Mastercam Files (*.mcam)", 2875 "Mastercam X Files
   (*.mcx*)", 2857 "All Mastercam Files (*.mc*)" (default), the "Import" types: FUN_18034c200 (mccoreres 2725 IGES, 2723 AutoCAD, 2730 STEP, 2746 StereoLithography (.stl, McStl after McAscii in FUN_180358740), the others not read here), 2858 "All Files (*.*)"); DWG / DWF are listed by Mastercam, refused here (no reader, formats/cad_import.md);
   Save As (SaveFile / FUN_180355810, 7284 "Save As") 33207 "Mastercam 2017 Files (*.mcam)" + "All Files (*.*)". The
   Windows common dialog itself is the platform's (Qt) file dialog here.
   A part from a newer release than Mastercam 2017 (FV-1): DoFileDialog (decomp/mastercam_filedlg.c:413-424) shows mccoreres 5280 +
   "\n" + 5301 (MB_OK) and opens the dialog again; the current part stays (viewer/mcam_grp.h). */
void MainWindow::openDialog() {
    QString dir = qc_loaded() && qc_part_path()[0] ? QFileInfo(QString::fromUtf8(qc_part_path())).absolutePath()
                : qc_recent_count() ? QFileInfo(QString::fromUtf8(qc_recent(0))).absolutePath() : QString(".");
    QString sel = "All Mastercam Files (*.mc*)";
    for (;;) {
        QString p = QFileDialog::getOpenFileName(this, "Open", dir,
            "Mastercam Files (*.mcam);;Mastercam X Files (*.mcx*);;All Mastercam Files (*.mc*);;IGES Files (*.igs *.iges);;AutoCAD Files (*.dxf);;STEP Files (*.stp *.step);;StereoLithography Files (*.stl);;apprenticeCAM Project (*.mcamui);;All Files (*)", &sel);
        if (p.isEmpty()) return;
        if (qc_part_newer(p.toUtf8().constData())) { message("Open", QString::fromUtf8(qc_newer_dialog_msg())); dir = QFileInfo(p).absolutePath(); continue; }
        openPath(p);   /* (mcamui FDT_OPEN: + "apprenticeCAM Project (*.mcamui)") */
        return;
    }
}
void MainWindow::saveDialog() {
    if (!qc_loaded()) { message("Save as MCAM part", "Open a part, or start one with File > New."); return; }
    QString src = QString::fromUtf8(qc_part_path());
    QString ext = QString::fromUtf8(qc_save_ext());
    QString dir = src.isEmpty() ? QString(".") : QFileInfo(src).absolutePath();
    QString name = src.isEmpty() ? QString("untitled") + ext : QFileInfo(src).completeBaseName() + "-workbench" + ext;   /* mcamui export_mcam_dialog */
    QString filt = ext == ".mcam" ? "Mastercam 2017 Files (*.mcam);;All Files (*)" : "Mastercam X Files (*.mcx*);;All Files (*)";
    QString p = QFileDialog::getSaveFileName(this, "Save As", dir + "/" + name, filt);
    if (!p.isEmpty()) savePartAs(p);
}

void MainWindow::showBackstage(bool on) {
    stack_->setCurrentWidget(on ? (QWidget *)backstage_ : stack_->widget(0));
    ribbonBar()->setMinimumMode(on);                   /* the backstage view covers the ribbon's panels (mcamui: from RIB_Y down) */
    if (on) backstage_->refresh();
}

/* mcamui main(): like Mastercam, start with an empty part; then default to the machine's toolpath tab */
void MainWindow::startup() {
    if (!qc_loaded() && !qEnvironmentVariableIsSet("MCAMUI_SCRIPT")) newPart();
    for (int i = ui_.tabs.size() - 1; i >= 0; i--) if (ui_.tabs[i].context == machine_) { ribbonBar()->raiseCategory(cats_[i]); break; }
    refreshAll();
}

void MainWindow::selectOp(int i) { sel = i; msel_owner = -9; sim = -1; tpm_->refresh(); view_->redraw(); }
void MainWindow::expandOp(int i) { tpm_->setOpExpanded(i, true); }

/* the window's X / Alt+F4 / Exit: the unsaved-changes prompt first (mcamui.c SDL_QUIT -> request(ACT_EXIT)) */
void MainWindow::closeEvent(QCloseEvent *e) {
    if (g_regen_busy) { e->ignore(); return; }           /* not while OK's computation runs (Cancel first) */
    if (quitting_ || !doc_dirty) { e->accept(); return; }
    e->ignore();
    request(ACT_EXIT);
    /* the prompt was answered inside this event (No, or Yes and saved): doAction's close() is a no-op while this close is
       running (Qt ignores a nested close), so this close is the one that ends the program */
    if (quitting_) e->accept();
}

bool MainWindow::eventFilter(QObject *o, QEvent *e) {
    /* the main window's keys also while one of its chaining tool windows is the active one (mcamui: one window) */
    QWidget *aw = QApplication::activeWindow();
    bool ours = isActiveWindow() || !aw || (chainDlg_ && aw == chainDlg_) || (drillDlg_ && aw == drillDlg_) || (opDlg_ && aw == opDlg_) || (tlDlg_ && aw == tlDlg_)
                || (tmDlg_ && aw == tmDlg_) || (twDlg_ && aw == twDlg_) || (tfDlg_ && aw == tfDlg_) || (rnDlg_ && aw == rnDlg_) || (coolDlg_ && aw == coolDlg_) || (tlrWin_ && aw == tlrWin_)
                || (aw && qobject_cast<FloatDlg *>(aw) && aw->parentWidget() == this);   /* the phase 8 tool windows too */
    if (e->type() == QEvent::KeyPress && ours && QApplication::activeModalWidget() == nullptr && QApplication::activePopupWidget() == nullptr) {
        auto *k = static_cast<QKeyEvent *>(e);
        if (toolKey(k)) return true;                     /* the modal Tool Manager / wizard (mcamui: show_tm skips the keymap) */
        QWidget *fw0 = QApplication::focusWidget();
        bool text0 = qobject_cast<QLineEdit *>(fw0) || qobject_cast<QTextEdit *>(fw0) || qobject_cast<QPlainTextEdit *>(fw0) || qobject_cast<QAbstractSpinBox *>(fw0)
                     || qobject_cast<QLineEdit *>(o);           /* a key sent to a line edit (the tree's label edit) is the edit's */
        bool nomod = !(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
        /* the VCR accelerators (FUN_66d1ed30): S B N P R while backplotting, S / B / R in Verify (mcamui main loop, before the keymap) */
        if (!text0 && nomod && !k->isAutoRepeat() && bp_on && (k->key() == Qt::Key_S || k->key() == Qt::Key_B || k->key() == Qt::Key_N || k->key() == Qt::Key_P || k->key() == Qt::Key_R)) {
            bp_keys('A' + (k->key() - Qt::Key_A)); bp_frame(); refreshAll(); return true; }
        if (!text0 && nomod && !k->isAutoRepeat() && vf_on && (k->key() == Qt::Key_S || k->key() == Qt::Key_B || k->key() == Qt::Key_R)) {
            view_->withGL([k] { vf_keys('A' + (k->key() - Qt::Key_A)); vf_frame(); }); refreshAll(); return true; }
        /* the Paste prompt (5457): Enter or Esc continues -> merge (core_ui paste_clip) */
        if (!text0 && copyImagePick_ && k->key() == Qt::Key_Escape) { copyImage(true); return true; }   /* <Esc>: the entire screen (opmanres 3288) */
        if (!text0 && ns_deleting && k->key() == Qt::Key_Escape) { view_->withGL([] { ns_del_end(); }); refreshAll(); return true; }   /* Nesting Results Delete: get_event returns 0 (core_ui cui_nest.c) */
        if (!text0 && psel_open && k->key() == Qt::Key_Escape) { view_->withGL([] { psel_esc(); }); refreshAll(); return true; }   /* get_plane_info: get_event returns 1 */
        if (!text0 && zd_picking() && k->key() == Qt::Key_Escape) { view_->withGL([] { zd_pick = 0; cam_msg[0] = 0; }); refreshAll(); return true; }
        if (!text0 && selrun_kind && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter || k->key() == Qt::Key_Escape)) {   /* select_ents' End Selection / Esc */
            bool ok = k->key() != Qt::Key_Escape; view_->withGL([ok] { selrun_end(ok ? 1 : 0); }); refreshAll(); return true; }
        if (!text0 && paste_pending && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter || k->key() == Qt::Key_Escape)) { view_->withGL([] { paste_clip(); }); refreshAll(); return true; }
        if (keymap(k)) return true;
        /* the keys mcamui handles after the keymap: Esc ends a point pick (Alt+F12), Delete removes the selected
           entities (an operation selected in the tree: deleting it is not ported here - phase 6) */
        QWidget *fw = QApplication::focusWidget();
        bool text = qobject_cast<QLineEdit *>(fw) || qobject_cast<QTextEdit *>(fw) || qobject_cast<QPlainTextEdit *>(fw) || qobject_cast<QAbstractSpinBox *>(fw)
                    || qobject_cast<QLineEdit *>(o);
        if (!text && k->key() == Qt::Key_Escape && st_pick) { st_pick_escape(); refreshAll(); return true; }   /* the Stock Setup's pick ends (mcamui) */
        if (!text && k->key() == Qt::Key_Escape && show_stock_setup) { st_cancel(); refreshAll(); return true; }   /* the sheet's Cancel */
        if (!text && tb_pick && !zoom_window && (k->key() == Qt::Key_Escape || k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter)) {   /* Position...: Enter ends, Esc (core_ui tb_key) */
            tb_key(k->key() != Qt::Key_Escape); syncOps(); refreshAll(); return true; }
        if (!text && k->key() == Qt::Key_Escape && pp_hide_on && !zoom_window) { pp_hide_end(); syncOps(); redrawView(); return true; }   /* batch 12: "Press <ESC> when done" */
        if (!text && k->key() == Qt::Key_Escape && (zoom_window || analyze_pick || rotposPick_ || lk_pick)) {   /* mcamui's Esc chain */
            zoom_window = analyze_pick = 0; zw_active = 0; rotposPick_ = false; lk_pick = 0; cam_msg[0] = 0; if (ViewInput *in = viewInput(view_)) in->zoomDrag = false; redrawView(); return true; }
        if (!text && k->key() == Qt::Key_Escape && show_help) { show_help = 0; syncMisc(); return true; }
        if (!text && k->key() == Qt::Key_Escape && az_i >= 0) { view_->withGL([] { az_close(1); }); refreshAll(); return true; }   /* Esc commits (slot 112) */
        if (!text && k->key() == Qt::Key_Escape && stack_->currentWidget() == backstage_) { showBackstage(false); return true; }
        if (!text && k->key() == Qt::Key_Escape && show_gridset) { gs_close(); syncMisc(); return true; }
        if (!text && k->key() == Qt::Key_Escape && srf_cmd) { view_->withGL([] { srf_end(); }); refreshAll(); return true; }   /* a Surfaces command ends (cui_surf.c) */
        if (!text && k->key() == Qt::Key_Escape && geo_cmd) { view_->withGL([] { geo_escape(); }); refreshAll(); return true; }   /* Esc / Cancel (geo_live_kind) */
        /* mcamui's Esc after the command: the Extrude command (ex_escape: a drag, a pick, the chain selection from the
           panel, else Cancel), the op dialog (phase 6: show_params off, then as cam_mode), toolpath chaining (cam_mode = 0,
           cam_clear_selection) */
        if (!text && k->key() == Qt::Key_Escape && ex_mode) { view_->withGL([] { ex_escape(); }); refreshAll(); return true; }
        if (!text && k->key() == Qt::Key_Escape && fl_mode) { view_->withGL([] { fl_escape(); }); refreshAll(); return true; }   /* Constant Fillet (cui_fillet.c) */
        if (!text && k->key() == Qt::Key_Escape && show_params) { pp_cancel(); refreshAll(); return true; }   /* core_ui pp_cancel (mcamui Esc) */
        if (!text && k->key() == Qt::Key_Escape && cam_mode && chain_escape()) { refreshAll(); return true; }   /* the solid selection (core_ui cui_sldchain.c) */
        if (!text && k->key() == Qt::Key_Escape && cam_mode) { chain_cancel(); refreshAll(); return true; }
        /* Esc with no function running: the general selection is cleared (core_ui sel_escape_idle: main frame PreTranslateMessage
           FUN_18026ad90 / COpMgrTree::PreTranslateMessage @180364f70 post 0x802a, CMastercamApp::Run @180294550 -> mccore
           PumpPreSelection @1804e6890 -> 0x8104 -> FUN_1804e07a0 set_clr_sel_bits_all(0, 4, ..), repaint). Only with no command,
           prompt, pick, drag or dialog of ours running (Mastercam: a running function takes the Esc through get_event) and with the
           main window active (a dialog / floating window's own Esc is not the main frame's; ours: Esc in a floating pane = nothing,
           its routing NOT DECODED); the focus in a text field keeps the key (`text`, as UIEdit) */
        if (!text && k->key() == Qt::Key_Escape && escIdle(aw) && sel_escape_idle()) { refreshAll(); return true; }
        /* Enter ends a polygon / vector being sketched (mcamui main loop: polygon_end / vector_end) */
        if (!text && k->key() == Qt::Key_A && !(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) && ds_running()) { view_->withGL([] { geo_ds_key(0xb); }); refreshAll(); return true; }   /* dyno_get_event: the key 'A' ends modes 3 and 5 like Enter (dyno_surf (sVar8 - 3U & 0xfffd) == 0 && key 'A'); the key character is taken as the letter key */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && (geo_cmd == G_CEDGEA || (geo_cmd == G_STRIMS && srf_trims_phase() < 2))) { view_->withGL([] { geo_ce_enter(); }); refreshAll(); return true; }
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && si_cmd(geo_cmd)) { view_->withGL([] { si_enter_key(); }); refreshAll(); return true; }
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && pln_cmd(geo_cmd)) { view_->withGL([] { pln_enter_key(); }); refreshAll(); return true; }   /* Curve Parting Line: <Enter> rebuilds (cui_partline.c) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && ds_running()) { view_->withGL([] { geo_ds_key(0xb); }); refreshAll(); return true; }   /* dyno_surf (cui_dsurf.c): Enter ends it */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && geo_cmd == G_LNORM && wm_ln_active()) { view_->withGL([] { geo_panel_button(1); }); refreshAll(); return true; }   /* Line Normal: the prompt 6707 "Press Enter to select another surface or face" = the panel code 1 path (FUN_1803584a0; the key to code mapping [inferred] from the prompt) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && geo_cmd == G_LETTERS && !geo_chaining) { view_->withGL([] { geo_panel_button(0); }); refreshAll(); return true; }   /* Create Letters: Enter = OK, the live letters committed (ours, user decision LET-1) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && wm_rg_wait) { wm_rg_enter(); refreshAll(); return true; }   /* Relief Groove Preview: wait_for_space (core_ui cui_wfmisc.c) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && sp_cmd(geo_cmd)) { view_->withGL([] { sp_enter_key(); }); refreshAll(); return true; }   /* the spline commands (cui_spline.c): finish / run */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && chain_sketching()) { view_->withGL([] { chain_sketch_end(); }); refreshAll(); return true; }
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && srf_solid_selecting()) { view_->withGL([] { srf_solid_enter(); }); refreshAll(); return true; }   /* From Solid's selection ends (cui_geo.c) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && (srf_sn_selecting() || srf_of_selecting())) { view_->withGL([] { srf_ned_enter(); }); refreshAll(); return true; }   /* Set Normals' End Selection (cui_geo.c) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && bb_selecting()) { view_->withGL([] { bb_enter(); }); refreshAll(); return true; }   /* the Bounding Box panel's Manual selection ends (mcamui) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && drv_pick) { view_->withGL([] { drv_done(); }); refreshAll(); syncOps(); return true; }   /* the drive / check prompt's End Selection (core_ui cui_chain.c) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && xdyn_on()) { xdy_ctrl_held = (k->modifiers() & Qt::ControlModifier) != 0; view_->withGL([] { xdyn_enter(); }); refreshAll(); return true; }   /* Dynamic: Enter = panel code 1 (FUN_1800e58f0); Ctrl+Enter keeps */
        if (!text && k->key() == Qt::Key_Control && !k->isAutoRepeat() && xdyn_on()) { bool t = false; view_->withGL([&] { t = xdyn_ctrl() != 0; }); if (t) { refreshAll(); return true; } }   /* Dynamic: Ctrl toggles gnomon / geometry (FUN_1800eb0a0) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && xf_enter_ok()) { view_->withGL([] { xf_enter(); }); refreshAll(); return true; }   /* a Transform command's select_ents ends (core_ui cui_xform.c) */
        /* Temporary Midpoints capture key N (core_ui geo_tmid_key; Alt / Ctrl / Shift up, FUN_1800f73c0) */
        if (!text && k->key() == Qt::Key_N && !(k->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier)) && !k->isAutoRepeat()) { if (geo_tmid_key()) { redrawView(); return true; } }
        /* the Extrude panel (formats/extrude.md 1.3): Enter = OK-New / Apply (OK when ev+0x150 is set: NOT DECODED); Delete
           with chains selected in the Chains list = Remove (mcamui main loop) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && ex_mode && ex_phase && !ex_pick && !ex_drag) { view_->withGL([] { ex_accept(1); }); refreshAll(); return true; }
        if (!text && k->key() == Qt::Key_Delete && ex_mode && ex_phase && ex_any_sel()) { ex_remove_sel(); refreshAll(); return true; }
        /* Constant Fillet: Enter ends the pick (sld_array_pick's End Selection) / in the panel = OK-New (FUN_180156ec0, ev+0x150 == 0);
           Delete = Remove of the selected list entries (KeyBinding Delete -> DeleteSolidEntityCommand) */
        if (!text && (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && fl_mode) { view_->withGL([] { if (fl_phase == 0) fl_sel_done(); else fl_accept(1); }); refreshAll(); return true; }
        if (!text && k->key() == Qt::Key_Delete && fl_mode && fl_phase == 1) { view_->withGL([] { fl_remove_sel(); }); refreshAll(); return true; }
        /* Delete goes by the focused window (formats/align_ops.md 2): the Toolpaths manager -> its ops (COpMgrTree::ExecuteKeymap
           VK_DELETE -> OMOnDeleteKey; consumed even with nothing selected there); anywhere else -> delete_entities (the main frame's
           PreTranslateMessage) */
        if (!text && k->key() == Qt::Key_Delete && tpmFocused()) { if (!show_params) { view_->withGL([] { delete_selected_op(); }); refreshAll(); } return true; }
        if (!text && k->key() == Qt::Key_Delete && nselected > 0) { view_->withGL([] { delete_selected_ents(); }); refreshAll(); return true; }
    }
    return SARibbonMainWindow::eventFilter(o, e);
}

/* nothing of ours that would take the Esc as a running Mastercam function does (get_event / a modal loop): every state the Esc
   chain above handles, plus the ones that run without an Esc of their own here (backplot, verify, the drive / check prompt, the
   gnomon, a surface drag, the selection prompts, the point picks, the Tool Manager and its dialogs); OK's computation
   (g_regen_busy) takes no key */
bool MainWindow::escIdle(QWidget *aw) const {
    if (g_regen_busy || (aw && aw != this)) return false;
    if (bp_on || vf_on || drv_pick || xdyn_on() || ds_running() || bb_selecting() || bb_dragging() || srf_solid_selecting() || chain_sketching() || wm_rg_wait
        || plg_picking() || zd_picking() || tb_pick || st_pick || show_stock_setup || geo_cmd || srf_cmd || ex_mode || fl_mode || ex_drag || show_params || cam_mode
        || show_chain_opts || show_tm || tedit_index > -2 || cool.open || selrun_kind || paste_pending || ns_deleting || zoom_window || analyze_pick
        || lk_pick || pp_hide_on || az_i >= 0 || show_gridset || show_help || copyImagePick_ || rotposPick_ || stack_->currentWidget() == backstage_) return false;
    return true;
}
bool MainWindow::tpmFocused() const {                  /* keyboard focus inside the Toolpaths manager (the op tree, COpMgrTree) */
    QWidget *f = QApplication::focusWidget();
    return tpm_ && f && (f == tpm_ || tpm_->isAncestorOf(f));
}
void MainWindow::keyPressEvent(QKeyEvent *e) {
    /* graphics-window keys go to the view (FUN_1803222f0); the keymap runs first (eventFilter) */
    if (ViewInput *in = viewInput(view_)) if (in->key(e, true)) return;
    SARibbonMainWindow::keyPressEvent(e);
}

/* ------------------------------------------------------------------------------------------------ phase 8 windows */
/* the windows follow core_ui's model as mcamui draws them every frame (draw_backplot / draw_bp_vcr / draw_bp_stops /
   draw_verify / draw_analyze / draw_color_dialog / draw_level_dialog / draw_edit_common / draw_gridset / draw_key_windows):
   shown while their flag is set, refreshed, hidden after. Positions as mcamui (ours, the originals' are NOT DECODED): Backplot
   and Verify at the top left of the area under the ribbon (4, TOP_H + 4), Analyze at the top right, the rest centred. */
QString MainWindow::cmdLabelOf(const QString &fn) const { return cmdLabel(fn); }
void MainWindow::syncMisc() {
    if (syncingMisc_ || g_qt_scripting) return;          /* mcamui draws nothing before its script has run (its frames fill the dialogs' texts) */
    syncingMisc_ = true;
    auto centre = [this](QWidget *d) { placeRight(d); };   /* ours: the right-hand edge (placeRight), no longer centred */
    auto reg = [this](FloatDlg *d) { connect(d, SIGNAL(changed()), this, SLOT(refreshAllSlot())); };
    if (!bpDlg_) {
        bpDlg_ = new BackplotDialog(this); vcr_ = new BpVcrBar(view_->widget()); vcr_->hide();
        bpoDlg_ = new BpOptionsDialog(this); csDlg_ = new BpStopsDialog(this);
        vfWin_ = new SimulatorWindow(view_, this);           /* Verify = the Mastercam Simulator window (simwin.cpp, formats/simulator.md) */
        connect(vfWin_, &SimulatorWindow::changed, this, [this] { refreshAll(); view_->redraw(); });
        azDlg_ = new AnalyzeDialog(this); colDlg_ = new ColorPicker(this); lvlDlg_ = new LevelPicker(this); ecpDlg_ = new EcpDialog(this);
        dopDlg_ = new DirtyOpsDialog(this);
        connect(dopDlg_, &DirtyOpsDialog::answered, this, [this](int ok) { int a = dop_answer(ok); if (a) dopResume(a); refreshAll(); });   /* CDirtyOpsDlg OK / Cancel */
        gridDlg_ = new GridDialog(this); stockDlg_ = new StockSetupDialog(this); reg(stockDlg_); aboutWin_ = new AboutWindow(this); keysWin_ = new KeysWindow(this, [](const QString &f) { return cmdLabel(f); });
        azDlg_->withGL = ecpDlg_->withGL = [this](const std::function<void()> &fn) { if (g_mw) view_->withGL(fn); };   /* not while the window is destroyed (a focused edit's focus-out) */
        clDlg_ = new ChangeLevelsDlg(this); atDlg_ = new AttribsDlg(this);   /* Home > Change Level / Set All (homedlg.cpp) */
        for (FloatDlg *d : std::initializer_list<FloatDlg *>{ bpDlg_, bpoDlg_, csDlg_, azDlg_, colDlg_, lvlDlg_, ecpDlg_, gridDlg_, aboutWin_, keysWin_ }) reg(d);
        for (FloatDlg *d : std::initializer_list<FloatDlg *>{ clDlg_, atDlg_ }) { d->hide(); connect(d, SIGNAL(changed()), this, SLOT(syncMiscSlot())); }
        connect(vcr_, &BpVcrBar::changed, this, [this] { refreshAll(); });
        /* the play timer: Backplot's Play (FUN_66d84490, at most 50 updates / s) and spin, Verify's Play (mcamui: every frame) */
        play_ = new QTimer(this);
        connect(play_, &QTimer::timeout, this, [this] {
            if (g_qt_scripting || g_regen_busy) return;      /* nothing plays while OK's computation reads the part */                     /* mcamui runs its script before the first frame: nothing plays between steps */
            if (bp_on && (bp_playing || (bp_opt.spin_tool && bp_tool_on))) { bp_frame(); if (bpDlg_) bpDlg_->refresh(); if (vcr_) vcr_->update(); updateStatusShade(); view_->redraw(); }
            if (vf_on && (vf_playing || vf_dirty)) { view_->withGL([] { vf_frame(); }); if (vfWin_) vfWin_->refresh(); view_->redraw(); }
        });
        play_->start(20);
    }
    if (bp_on) {
        if (!g_qt_scripting) bp_frame_still();
        bool fresh = !bpDlg_->isVisible() || bp_new;
        bpDlg_->refresh();
        if (fresh) { placeRight(bpDlg_); bpDlg_->show(); bp_new = 0; }
        QWidget *vw = view_->widget(); vcr_->setGeometry(0, 1, vw->width(), 30); vcr_->show(); vcr_->raise(); vcr_->update();   /* CreateSlider: (0, 1), client width x 30 */
    } else { if (bpDlg_->isVisible()) bpDlg_->hide(); vcr_->hide(); bpo_open = 0; }
    if (bp_on && bpo_open) { bool f = !bpoDlg_->isVisible(); bpoDlg_->refresh(); if (f) { centre(bpoDlg_); bpoDlg_->show(); } bpoDlg_->raise(); } else if (bpoDlg_->isVisible()) bpoDlg_->hide();
    if (bp_on && bp_show_cs) { bool f = !csDlg_->isVisible(); csDlg_->refresh(); if (f) { centre(csDlg_); csDlg_->show(); } } else if (csDlg_->isVisible()) csDlg_->hide();
    if (sst_ && cui_status_gen != sstGen_) { sstGen_ = cui_status_gen; sst_->setText(QString::fromUtf8(cui_status)); sst_->setVisible(cui_status[0] != 0); }
    if (sbp_) { bool on = bp_on != 0; if (on) { char t[120]; bp_status_text(t, sizeof t); sbp_->setText(t); }
                sbp_->setVisible(on); sx_->setVisible(!on); sy_->setVisible(!on); sz_->setVisible(!on); }
    if (vf_on) {
        if (!g_qt_scripting) view_->withGL([] { vf_frame_still(); });
        bool fresh = !vfWin_->isVisible() || vf_newwin; vfWin_->refresh();
        if (fresh) { vfWin_->show(); vfWin_->raise(); vf_newwin = 0; }   /* its own window at UbvsPersistant Left / Top (simwin.cpp) */
    } else if (vfWin_->isVisible()) vfWin_->hide();
    if (az_i >= 0 && (az_ent >= 0 || az_sol >= 0)) {
        bool fresh = !azDlg_->isVisible() || az_open_new; azDlg_->refresh();
        if (fresh) { placeRight(azDlg_); azDlg_->show(); az_open_new = 0; }
    } else if (azDlg_->isVisible()) azDlg_->hide();
    if (cdlg_open && !cdlg_picking) { bool f = !colDlg_->isVisible() || cdlg_new; colDlg_->refresh(); if (f) { centre(colDlg_); colDlg_->show(); cdlg_new = 0; } colDlg_->raise(); }
    else if (colDlg_->isVisible()) colDlg_->hide();
    if (ldlg_open && !ldlg_picking) { bool f = !lvlDlg_->isVisible() || ldlg_new; lvlDlg_->refresh(); if (f) { centre(lvlDlg_); lvlDlg_->show(); ldlg_new = 0; } lvlDlg_->raise(); }
    else if (lvlDlg_->isVisible()) lvlDlg_->hide();
    if (show_ecp) { bool f = !ecpDlg_->isVisible() || ecp_new; ecpDlg_->refresh(); if (f) { centre(ecpDlg_); ecpDlg_->show(); ecp_new = 0; } } else if (ecpDlg_->isVisible()) ecpDlg_->hide();
    /* the new Lathe Face sheet (core_ui cui_lface.c: lathe_face 0x1801a3290 -> edit_lathe_operation) */
    if (!lfDlg_) { lfDlg_ = new LfaceDialog(this); lfDlg_->hide(); reg(lfDlg_); connect(lfDlg_, SIGNAL(changed()), this, SLOT(syncMiscSlot())); lfDlg_->withGL = [this](const std::function<void()> &fn) { if (g_mw) view_->withGL(fn); }; }
    if (show_lface) { bool f = !lfDlg_->isVisible(); lfDlg_->refresh(); if (f) { centre(lfDlg_); lfDlg_->show(); } lfDlg_->raise(); } else if (lfDlg_->isVisible()) lfDlg_->hide();
    /* opmanres dialog 977 (core_ui cui_dirty.c): shown while a Post / Backplot / Verify waits for it (Mastercam: DoModal) */
    if (dop_pending) { bool f = !dopDlg_->isVisible(); dopDlg_->refresh(); if (f) { centre(dopDlg_); dopDlg_->show(); } dopDlg_->raise(); }
    else if (dopDlg_->isVisible()) dopDlg_->hide();
    if (show_gridset) { bool f = !gridDlg_->isVisible(); gridDlg_->refresh(); if (f) { placeRight(gridDlg_); gridDlg_->show(); } }
    else if (gridDlg_->isVisible()) gridDlg_->hide();
    /* Machine Group Properties (core_ui cui_stock.c): hidden during its graphics picks (AfxSuspendWindow / AfxResumeWindow) */
    if (show_stock_setup && !st_pick && !st_browse) { bool f = !stockDlg_->isVisible(); stockDlg_->refresh(); if (f) { centre(stockDlg_); stockDlg_->show(); } stockDlg_->raise(); }
    else if (stockDlg_->isVisible()) stockDlg_->hide();
    if (show_stock_setup && st_pick == ST_PICK_PLANE) syncOps();
    /* Transform > Geometry Nesting (core_ui cui_nest.c): the property sheet, its Configuration dialog over it, the Results */
    if (!nestDlg_[0]) {
        static const int tpl[3] = { NS_SHEET, 201, 205 };
        for (int k = 0; k < 3; k++) {
            nestDlg_[k] = new NestDialog(tpl[k], this); nestDlg_[k]->hide();
            nestDlg_[k]->withGL = [this](const std::function<void()> &fn) { if (g_mw) view_->withGL(fn); };
            connect(nestDlg_[k], &NestDialog::changed, this, [this] { syncMisc(); syncChain(); refreshAll(); });
        }
    }
    {
        const int on[3] = { ns_open, ns_cfg_open, ns_res_open && !ns_deleting };   /* Results Delete suspends the dialog (AfxSuspendWindow) */
        for (int k = 0; k < 3; k++) {
            if (on[k]) { bool f = !nestDlg_[k]->isVisible(); nestDlg_[k]->refresh(); if (f) { centre(nestDlg_[k]); nestDlg_[k]->show(); } nestDlg_[k]->raise(); }
            else if (nestDlg_[k]->isVisible()) nestDlg_[k]->hide();
        }
    }   /* the stock plane's Plane Selection (cui_planes.c pls_which 3) */
    if (st_browse && !stlBrowse_ && !g_qt_scripting) {     /* 1253: FUN_1801ef850 PromptForFileToOpen(path, "rb", ".stl") */
        stlBrowse_ = true;
        QTimer::singleShot(0, this, [this] {
            QString cur = QString::fromUtf8(stp.file), dir = cur.isEmpty() ? QString() : QFileInfo(cur).absolutePath();   /* the default folder (stl_prefix): NOT DECODED */
            QString p = QFileDialog::getOpenFileName(this, "Open", dir, "STL Files (*.stl);;All Files (*.*)");
            stlBrowse_ = false;
            if (p.isEmpty()) st_browse_done(nullptr); else st_browse_done(p.toUtf8().constData());
            refreshAll(); });
    }
    if (st_q2d && !g_qt_scripting) {                     /* create_bbox_stock_no_dlg: mccadres 17 / caption 30 (message_box_yes_no) */
        QTimer::singleShot(0, this, [this] { if (!st_q2d) return;
            int r = QMessageBox::question(this, "Bounding Box", "Override 2D construction mode?", QMessageBox::Yes | QMessageBox::No);
            st_q2d_answer(r == QMessageBox::Yes); refreshAll(); });
    }
    if (show_about) { if (!aboutWin_->isVisible()) { centre(aboutWin_); aboutWin_->show(); } } else if (aboutWin_->isVisible()) aboutWin_->hide();
    if (show_help) { if (!keysWin_->isVisible()) { keysWin_->refresh(); placeRight(keysWin_); keysWin_->show(); } } else if (keysWin_->isVisible()) keysWin_->hide();
    if (ask.act == CM_GRP_RENAME && renameShown_ != CM_GRP_RENAME && !g_qt_scripting) { renameShown_ = CM_GRP_RENAME; showManager(0); tpm_->beginRename(cm_group); }   /* TVM_EDITLABEL */
    if (ask.act == CM_OP_RENAME && renameShown_ != CM_OP_RENAME && !g_qt_scripting) { renameShown_ = CM_OP_RENAME; showManager(0); tpm_->beginOpRename(op_renaming()); }   /* an op's label edit (TVS_EDITLABELS) */
    if (!ask.act) renameShown_ = 0;
    if (want_print) QTimer::singleShot(0, this, [this] { printNow(); });   /* mcamui: the frame after the command (want_print) */
    /* the zoom-window drag (OnScreenZoom) and the graphics cross cursor follow the view */
    syncingMisc_ = false;
}
void MainWindow::refreshAllSlot() { refreshAll(); }
/* dialog 977's OK (cui_dirty.c dop_answer): the command it held, once without the check (dop_bypass) */
void MainWindow::dopResume(int a) {
    if (a == DOP_POST) post_ops();
    else if (a == DOP_BACKPLOT) { view_->withGL([] { bp_start(); if (bp_on) bp_frame_still(); }); syncMisc(); }
    else if (a == DOP_VERIFY) { view_->withGL([] { vf_start(); if (vf_on) vf_frame_still(); }); updateStatusShade(); syncMisc(); }
    refreshAll(); view_->redraw();
}
/* OnFilePrint (INTENTIONAL, formats/provenance_audit.md: Mastercam's print pipeline is not ported): a picture of the window
   next to the part / project (core_ui print_path), as mcamui's "print-<time>.bmp" */
bool MainWindow::printNow() {
    if (!want_print) return false;
    want_print = 0;
    char out[1200]; print_path(out, sizeof out);
    bool ok = grab().save(QString::fromUtf8(out), "BMP");
    print_done(out, ok); redrawView();
    return ok;
}
/* ours (tests): the ribbon command at PATH (parityDump's path: Tab/Group/Label[/Sub label]) run as a click on its button (invoke) */
bool MainWindow::invokeRibbonPath(const QString &want) {
    const UiCmd *hit = nullptr;
    std::function<void(const QString &, const UiCmd &)> find = [&](const QString &path, const UiCmd &c) {
        QString p2 = path + "/" + (c.text.isEmpty() ? c.tip : c.text);
        if (!hit && p2 == want) hit = &c;
        for (const UiCmd &sc : c.sub) find(p2, sc);
    };
    for (const UiTab &t : ui_.tabs) for (const UiGroup &g : t.g) {
        QString path = t.name + (t.context.isEmpty() ? QString() : "@" + t.context) + "/" + g.name;
        for (const UiElem &e : g.el) if (!e.is_gallery) find(path, e.cmd);
    }
    if (!hit) return false;
    printf("    ribbon %s (%s): %s\n", want.toUtf8().constData(), hit->ft.toUtf8().constData(), cmdOk(*hit) ? "on" : "off");
    invoke(*hit);
    return true;
}
/* tests (ours): galitem:Tab@Context|Gallery|Item[#n] - a ribbon gallery item as its click runs it (greyed: only said) */
bool MainWindow::invokeGalleryItem(const QString &key) {
    for (int i = 0; i < galActs_.size() && i < galKeys_.size(); i++) if (galKeys_[i] == key) {
        printf("    gallery %s: %s\n", key.toUtf8().constData(), galActs_[i].first->isEnabled() ? "on" : "off");
        if (galActs_[i].first->isEnabled()) galActs_[i].first->trigger();
        return true;
    }
    return false;
}
/* the parity check (tools/tests/parity_check.sh): the same lines as mcamui.c parity_dump */
void MainWindow::parityDump() {
    std::function<void(const QString &, const UiCmd &)> cmd = [&](const QString &path, const UiCmd &c) {
        QString p2 = path + "/" + (c.text.isEmpty() ? c.tip : c.text);
        printf("ribbon\t%s|%s\t%s\n", p2.toUtf8().constData(), c.ft.toUtf8().constData(), cmdOk(c) ? "on" : "off");
        for (const UiCmd &sc : c.sub) cmd(p2, sc);
    };
    for (const UiTab &t : ui_.tabs) for (const UiGroup &g : t.g) {
        QString path = t.name + (t.context.isEmpty() ? QString() : "@" + t.context) + "/" + g.name;
        for (const UiElem &e : g.el) {
            if (e.is_gallery) { for (const auto &it : e.items) printf("gallery\t%s/%s/%s\t%s\n", path.toUtf8().constData(), e.gname.toUtf8().constData(), it.name.toUtf8().constData(),
                                                                   cam_type_from_label(it.name.toUtf8().constData()) ? "on" : "off"); continue; }
            if (e.cmd.kind == "Group") { for (int k = 0; k < e.cmd.sub.size(); k++) cmd(path, e.cmd.sub[k]); continue; }
            cmd(path, e.cmd);
        }
    }
    for (const UiTbBtn &b : ui_.tbtns) { UiCmd c; c.text = b.tip; c.tip = b.tip; printf("toolbar\t%d/%s\t%s\n", b.tb, b.tip.toUtf8().constData(), cmdOk(c) ? "on" : "off"); }
    char q[16][64]; int nq = cui_qat_load(q);
    for (int i = 0; i < nq; i++) if (q[i][0]) printf("qat\t%s\t%s\n", q[i], mcFunction(QString::fromUtf8(q[i]), true) ? "on" : "off");
    for (int i = 0; i < cui_nkmap; i++) printf("key\t%s=%s\t%s\n", cui_kmap[i].keys, cui_kmap[i].fn, mcFunction(QString::fromUtf8(cui_kmap[i].fn), true) ? "on" : "off");
    for (int d = 0; d < nrmb_drop; d++) for (int i = 0; i < rmb_drop[d].n; i++) if (rmb_drop[d].it[i].kind == 0)
        printf("rmb\t%s/%s\t%s\n", rmb_drop[d].name, rmb_drop[d].it[i].fn, mcFunction(QString::fromUtf8(rmb_drop[d].it[i].fn), true) ? "on" : "off");
    for (int i = 0; i < BS_NITEMS; i++) printf("backstage\t%s\t%s\n", BS_ITEMS[i].text, backstage_->itemEnabled(i) ? "on" : "off");
    static const int pages[] = { 107, 105, 140, 115, 111, 130 };
    for (int pg : pages) { int n; const BsCtl *c = bs_page_ctls(pg, &n);
        for (int i = 0; i < n; i++) if (c[i].cls == 1) printf("bspage\t%d/%u/%s\t%s\n", pg, c[i].id, c[i].text, backstage_->pageButtonEnabled(pg, c[i].id) ? "on" : "off"); }
    fflush(stdout);
}

MainWindow::~MainWindow() { g_mw = nullptr; }
void MainWindow::showBackstagePage(int item) { showBackstage(true); if (item >= 0) backstage_->showPage(item); }
void MainWindow::openDialogShown() {                    /* mcamui show:open: its file dialog drawn for the shot (here a non-modal QFileDialog) */
    auto *d = new QFileDialog(this, "Open", qc_loaded() && qc_part_path()[0] ? QFileInfo(QString::fromUtf8(qc_part_path())).absolutePath() : QString("."),
        "Mastercam Files (*.mcam);;Mastercam X Files (*.mcx*);;All Mastercam Files (*.mc*);;apprenticeCAM Project (*.mcamui);;All Files (*)");
    d->setAttribute(Qt::WA_DeleteOnClose); d->setOption(QFileDialog::DontUseNativeDialog); d->selectNameFilter("All Mastercam Files (*.mc*)");
    connect(d, &QFileDialog::fileSelected, this, [this](const QString &p) { openPath(p); });
    d->show();
}
