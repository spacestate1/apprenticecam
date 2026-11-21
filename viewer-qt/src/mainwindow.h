// mainwindow - the apprenticeCAM main window in Qt: SARibbon from viewer/ui/layout.txt, the ADS dock with the
// managers on the left, the graphics view (scene.c) in the centre, the status bar. Command behaviour and citations
// are moved from viewer/mcamui.c (run_local_x, run_mc_function, draw_ribbon, draw_qat, draw_status, do_action).
#pragma once
#include "SARibbonMainWindow.h"
#include "uilayout.h"
#include "managers.h"
#include <QStringList>
#include <QHash>

class SARibbonCategory; class SARibbonContextCategory; class QAction; class QLabel; class QToolButton; class QStackedWidget;
class ViewApi; class Backstage; class OpDialog; class ToolListDialog; class PlanesDialog; class ToolManagerDialog; class ToolFilterDialog; class ReportWindow; class RenumberDialog;
class ToolWizardDialog; class CoolantDialog; class GeoPanel; class BackplotDialog; class BpVcrBar; class BpOptionsDialog; class BpStopsDialog; class SimulatorWindow;
class AnalyzeDialog; class ColorPicker; class LevelPicker; class EcpDialog; class GridDialog; class DirtyOpsDialog; class StockSetupDialog; class NestDialog; class AboutWindow; class KeysWindow; class QTimer; class ExtrudePanel; class ChainDialog; class DrillDialog; class ChainOptsDialog; class RefFaceDialog;
namespace ads { class CDockManager; class CDockWidget; class CDockAreaWidget; }

class MainWindow : public SARibbonMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(UiLayout &ui, QWidget *parent = nullptr);
    ~MainWindow() override;
    // commands (by label: the ribbon / menus / toolbars; by FT function: QAT / keymap)
    bool localRun(const QString &t, bool probe);       // mcamui.c run_local_x
    bool cmdOk(const UiCmd &c);                        // mcamui.c cmd_ok
    void invoke(const UiCmd &c);                       // mcamui.c invoke
    bool mcFunction(const QString &fn, bool probe);    // mcamui.c run_mc_function (the ported part)
    // document (core_ui cui_doc.c): every action that drops the document asks first when it has unsaved changes
    enum { ACT_NONE, ACT_NEW, ACT_OPEN, ACT_OPEN_PATH, ACT_CLOSE, ACT_EXIT };   // mcamui.c request / do_action
    void request(int act, const QString &path = QString());
    void doAction(int act, const QString &path);
    void answerPrompt(int pick);                       // the unsaved-changes prompt: 1 Yes (save), 2 No
    int pendingPrompt() const { return confirmAct_; }
    void openPath(const QString &p);
    void newPart();
    void closePart();
    bool savePartAs(const QString &p);                 // a Mastercam part (mcam_saveops.c)
    void docSave(bool as);                             // the .mcamui project (mcamui.c doc_save)
    bool docSaveTo(const QString &p);
    // the graphics view (core_ui selection / picking)
    void viewClick(float x, float y);
    void viewClickIn(float x, float y);                  // mcamui.c handle_click, the ported cases
    void viewMenu(const QPoint &global);               // the default.mtb right-click menu
    bool keymap(QKeyEvent *e);                         // CONFIG/mastercam.kmp (core_ui cui_kmap)
    void redrawView();                                 // overlay rebuilt + repaint (GL context current)
    void startup();                                    // mcamui main(): empty part, the machine's toolpath tab
    // wireframe commands (core_ui cui_geo.c): the function panel dock follows geo_cmd
    void syncGeo();                                    // panel shown / rebuilt / refreshed for the running command
    void geoStarted();                                 // cui_hooks.geo_started: the panel's tab to the front
    void geoButton(int code);                          // the panel's OK (0) / Apply (1) / Cancel (2)
    void showAcSettings();                             // AutoCursor Settings (opmanres dialog 779)
    // chaining (core_ui cui_chain.c): the Chaining / Drill Point Selection / Chaining Options dialogs follow the model
    void syncChain();                                  // shown / rebuilt / refreshed for the chaining state (mcamui draw_cam's chaining block)
    void chainAct(int id);                             // a Chaining dialog control (core_ui chain_act)
    void chainOk();                                    // cui_hooks.chain_ok: the chaining's consumer (the op dialog: phase 6; Extrude: phase 5)
    void chainAskUnselectAll();                        // cui_hooks.chain_ask_unselect_all (MCMessageBox 5007 / 10206)
    bool galleryPick(const QString &name);             // a toolpath gallery item (core_ui gallery_pick) -> its chaining
    void chainClose();                                 // the Chaining window's close box / Esc (cancel_chaining)
    // the toolpath dialog (core_ui cui_ops.c / cui_opdlg.c / cui_toolpage.c): OpDialog (6544) follows show_params, the Tool List (6410) tl6410_open
    void syncOps();
    void placeRight(QWidget *d);                         // ours: a dialog's first position, at the program's right-hand edge
    void openOpParams(int op);                         // the tree's Parameters row / a double click
    void opMenuRun(int item);                          // a Toolpaths manager menu item (core_ui cm_run)
    void treeExpand(bool on);                          // the menu's Expand / Collapse
    OpDialog *opDialog() { return opDlg_; }
    // N13-9: a new 3D op's generation after the picks that follow its dialog (core_ui cui_chain.c ms_generate -> cui_hooks.gen_queued)
    // runs as the op dialog's OK does (OpDialog::runJob: worker thread + progress window); scripted steps wait for it (testhooks.cpp)
    void queueGen();
    void runPendingGen();
    // N13-8: the Selection Bar's Select body / Select face buttons (core_ui ms_selbar_*); tests: selbarclick:body|face
    void updateSelBar();
    bool selBarClick(bool face);
    bool selBarTestClick(bool face);                   // tests: a click on the shown button (false: hidden = not enabled)
    ToolListDialog *toolListDialog() { return tlDlg_; }
    class PlaneSelDialog *planeSel1085() { return pselDlg_; }   // the Plane Selection 1085 of get_plane_info (core_ui cui_plsel.c)
    void syncPlaneSel();
    PlanesDialog *planeSelectDialog() { return plsDlg_; }   // the Planes page's Plane Selection 842 / Update Plane 10269 (core_ui cui_planes.c)
    PlanesDialog *updatePlaneDialog() { return uplDlg_; }
    ToolpathsManager *toolpathsManager() { return tpm_; }
    bool tpmFocused() const;
    bool escIdle(QWidget *aw) const;                   // Esc reaches the idle general selection (no command / prompt / dialog of ours runs)
    // phase 7 (core_ui cui_toolmgr.c / cui_toolwiz.c): the Tool Manager (1099), its filter (25519), the Define / Edit Tool wizard
    // + Coolant, the tool report window, Renumber tools (805) follow show_tm / tmf_open / tedit_index / cool.open / show_tlr / show_rn
    void syncTools();
    bool toolKey(QKeyEvent *k);                        // the modal Tool Manager / wizard take the keys (mcamui tm_keydown)
    ToolManagerDialog *toolManager() { return tmDlg_; }
    ToolFilterDialog *toolFilter() { return tfDlg_; }
    ToolWizardDialog *toolWizard() { return twDlg_; }
    CoolantDialog *coolantDialog() { return coolDlg_; }
    RenumberDialog *renumberDialog() { return rnDlg_; }
    ReportWindow *reportWindow() { return tlrWin_; }
    // phase 8 (core_ui cui_backplot.c / cui_verify.c / cui_analyze.c / cui_ecp.c / cui_misc.c): Backplot (14000 + the VCR bar
    // 15000 + Options 1228 + Conditional Stops 6000), Verify, Analyze + the Colors / Levels pickers, Edit Common Parameters
    // (243), Screen Grid (1589), About, the keymap window, the group rename edit - all follow the model
    void syncMisc();
    QString cmdLabelOf(const QString &fn) const;
    void parityDump();                                 // tests: every command and its state (tools/tests/parity_check.sh)
    bool invokeGalleryItem(const QString &key);        // tests: galitem:Tab@Context|Gallery|Item[#n] - a gallery item as its click runs it (ours)
    bool invokeRibbonPath(const QString &path);        // tests: ribbon:Tab/Group/Label[/Sub] - the ribbon command as its button runs it (ours)
    BackplotDialog *backplotDialog() { return bpDlg_; }
    BpVcrBar *vcrBar() { return vcr_; }
    BpOptionsDialog *bpOptions() { return bpoDlg_; }
    AnalyzeDialog *analyzeDialog() { return azDlg_; }
    ColorPicker *colorPicker() { return colDlg_; }
    LevelPicker *levelPicker() { return lvlDlg_; }
    EcpDialog *ecpDialog() { return ecpDlg_; }
    GridDialog *gridDialog() { return gridDlg_; }
    DirtyOpsDialog *dirtyOpsDialog() { return dopDlg_; }
    void dopResume(int action);                        // dialog 977 answered: the held command runs (core_ui cui_dirty.c)
    StockSetupDialog *stockDialog() { return stockDlg_; }
    NestDialog *nestDialog(int k) { return nestDlg_[k]; }   // Geometry Nesting: 0 the property sheet, 1 Configuration, 2 Results
    SimulatorWindow *verifyWindow() { return vfWin_; }
    bool printNow();                                   // OnFilePrint (INTENTIONAL: a BMP of the window next to the part)
    void showGalleryPopup();
    void showBackstagePage(int item);                  // tests: show:backstage:N (-1 = as it was)
    void openDialogShown();                            // tests: show:open - the Open dialog, not modal                           // tests: the Toolpaths tab's first gallery's palette open (show:gallery)
    // the Extrude command (core_ui cui_extrude.c): its panel dock follows ex_mode && ex_phase
    void syncExtrude();                                // panel shown / hidden / refreshed; the preview (ex_panel_frame) when not scripting
    void syncFillet();                                 // Constant Fillet: the Solid Selection / panel dock (core_ui cui_fillet.c)
    void flButton(int code);
    void exButton(int code);                           // the panel's OK (0) / Apply-Regen (1) / Cancel (2)
    ExtrudePanel *exPanel() { return ex_; }
    SolidsManager *solidsManager() { return sm_; }
    void msgClose() { msgShown_ = false; }             // cui_hooks.msg_close (and every script step's end, as mcamui dump_state)
    bool msgShown(const char **t, const char **x) const { *t = msgT_.constData(); *x = msgX_.constData(); return msgShown_; }
    ChainDialog *chainDialog() { return chainDlg_; }
    ChainOptsDialog *chainOptsDialog() { return optsDlg_; }
    GeoPanel *geoPanel() { return geo_; }
    // state for tests / scripts
    ViewApi *view() { return view_; }
    void setTabByName(const QString &name);
    QString currentTabName() const;
    void setManagerTab(int k);
    void refreshAll();
    void selectOp(int i);
    void expandOp(int i);
    const QString &lastMessage() const { return msg_; }
    void message(const QString &title, const QString &text);
    void refreshTree();
public slots:
    void refreshAllSlot();
protected:
    void closeEvent(QCloseEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;
private:
    void buildRibbon();
    void buildQat();
    void buildDocks();
    void buildStatus();
    void addCmd(class SARibbonPanel *panel, const UiCmd &c, bool large);
    QAction *makeAction(const UiCmd &c, int iconPx);
    void updateEnabled();
    void updateContexts();
    void updateTitle();
    void updateStatusShade();
    void addRecent(const QString &t);
    void openDialog();
    void saveDialog();
    void showBackstage(bool on);
    void showManager(int tab, bool toggle = false);

    UiLayout &ui_;
    QString machine_ = "Mill";
    QList<SARibbonCategory *> cats_;                   // per ui_.tabs index
    QHash<QString, SARibbonContextCategory *> ctx_;    // context name -> category
    struct ActRef { QAction *a; UiCmd c; };
    QList<ActRef> acts_;
    QList<QPair<QAction *, QString>> qat_;             // QAT action, FT function
    QStringList recent_;                               // Recent Functions (add_recent)
    QString msg_;
    QString rlFt_;                                     // the FT function of the command being probed / run (localRun's File labels)
    bool rlFtSet_ = false;
    bool rlNoGeo_ = false;                             // the command is another module's with a wireframe name (mcamui rl_nogeo)
    int confirmAct_ = 0; QString confirmPath_;         // the unsaved-changes prompt (scripted runs keep it open: answer:)
    int afterSaveAct_ = 0; QString afterSavePath_;     // the action that waits for the Save
    bool rotposPick_ = false;                          // Alt+F12: the next click sets the rotation centre
    bool quitting_ = false;
    ViewApi *view_ = nullptr;
    QStackedWidget *stack_ = nullptr; Backstage *backstage_ = nullptr;
    ads::CDockManager *dock_ = nullptr;
    QList<ads::CDockWidget *> mgrDocks_;
    ads::CDockAreaWidget *mgrArea_ = nullptr;
    ads::CDockWidget *geoDock_ = nullptr; GeoPanel *geo_ = nullptr; int shownLayout_ = 0, shownGeo_ = 0;
    ads::CDockWidget *exDock_ = nullptr; ExtrudePanel *ex_ = nullptr; bool shownEx_ = false;
    ads::CDockWidget *flDock_ = nullptr; class FilletPanel *fl_ = nullptr; bool shownFl_ = false;
    bool msgShown_ = false; QByteArray msgT_, msgX_;   /* the last message box (mcamui show_msg / msg_title / msg_text) */
    OpDialog *opDlg_ = nullptr; ToolListDialog *tlDlg_ = nullptr; PlanesDialog *plsDlg_ = nullptr, *uplDlg_ = nullptr; class PlaneSelDialog *pselDlg_ = nullptr;
    bool opSuspended_ = false;                          /* hidden for a Linking "..." pick (AfxSuspendWindow) */
    ToolManagerDialog *tmDlg_ = nullptr; ToolFilterDialog *tfDlg_ = nullptr; ReportWindow *tlrWin_ = nullptr; RenumberDialog *rnDlg_ = nullptr;
    ToolWizardDialog *twDlg_ = nullptr; CoolantDialog *coolDlg_ = nullptr; int tlrShown_ = 0;
    ChainDialog *chainDlg_ = nullptr; DrillDialog *drillDlg_ = nullptr; RefFaceDialog *refDlg_ = nullptr; ChainOptsDialog *optsDlg_ = nullptr; int chainLastN_ = -1;
    QList<QPair<QAction *, QString>> galActs_;
    QStringList galKeys_;                              // tests: each gallery action's "Tab@Context|Gallery|Item[#n]" (invokeGalleryItem)
    QList<QPair<QString, class SARibbonGallery *>> gals_;   // tab (context/name) -> its galleries
    QString galleryTip(const QString &key, const QString &name) const;         // the toolpath gallery items (enabled where gallery_pick generates the op)
    ToolpathsManager *tpm_ = nullptr; SolidsManager *sm_ = nullptr; LevelsManager *lm_ = nullptr; RecentManager *rm_ = nullptr;
    QLabel *sst_ = nullptr; int sstGen_ = 0;           /* UpdateStatusBarText's pane (core_ui cui_status) */
    QLabel *sx_ = nullptr, *sy_ = nullptr, *sz_ = nullptr, *sbp_ = nullptr; QLabel *splab_[3] = { nullptr, nullptr, nullptr };
    class PlanesManager *pm_ = nullptr;
    // Home > Attributes / Organize (core_ui cui_attr.c): the dialogs, the ribbon combos, the status bar's 2D / 3D
    class ChangeLevelsDlg *clDlg_ = nullptr; class AttribsDlg *atDlg_ = nullptr;
    QVector<QPair<int, class QComboBox *>> homeCombos_;   // ribbon id -> combo (53409 point style, 53410 line style, 53411 width, 53422 Z, 53424 level)
    class QToolButton *d3_ = nullptr;
    void syncHome();
    Q_SLOT void syncMiscSlot();
    void copyImage(bool whole);                        // Home > Copy Image (ToClipboard)
public:
    void openSelrun(int kind);                         // cui_hooks.selrun_dialog
    void copyImageTest(const QString &arg);            // tests: copyimage[:X0,Y0,X1,Y1] - Copy Image's Esc (whole) or two corners, then the clipboard image size
    bool copyImagePick_ = false; int ciX_[2] = { 0, 0 }, ciY_[2] = { 0, 0 }, ciN_ = 0;
private:
    BackplotDialog *bpDlg_ = nullptr; BpVcrBar *vcr_ = nullptr; BpOptionsDialog *bpoDlg_ = nullptr; BpStopsDialog *csDlg_ = nullptr; SimulatorWindow *vfWin_ = nullptr;
    AnalyzeDialog *azDlg_ = nullptr; ColorPicker *colDlg_ = nullptr; LevelPicker *lvlDlg_ = nullptr; EcpDialog *ecpDlg_ = nullptr; class LfaceDialog *lfDlg_ = nullptr; GridDialog *gridDlg_ = nullptr; DirtyOpsDialog *dopDlg_ = nullptr; StockSetupDialog *stockDlg_ = nullptr; NestDialog *nestDlg_[3] = { nullptr, nullptr, nullptr };
    AboutWindow *aboutWin_ = nullptr; KeysWindow *keysWin_ = nullptr; QTimer *play_ = nullptr; int renameShown_ = 0; bool stlBrowse_ = false;   /* the Stock Setup's STL file dialog is up */
    bool syncingMisc_ = false;
    QToolButton *shade_[6] = {};
    QToolButton *selBtn_[2] = {};                      // Select body / Select face (N13-8)
    bool genQueued_ = false;
};
