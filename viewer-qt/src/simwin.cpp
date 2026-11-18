/* simwin - the Mastercam Simulator window (formats/simulator.md). Sources, abbreviated in the comments:
 *   baml   = ubvs.dll mainwindow.baml (tools/baml_dump.py), the element named by its Id / Name
 *   resx   = UbvsResources MainWindowXaml.resx (the labels / tips; "Mastercam Simulator" debranded to apprenticeCAM)
 *   VM     = ubvs.dll UBVS.MainWindowViewModel, MW = UBVS.MainWindow, player = mwsimstreammanager MachineSimulationPlayer
 * Live controls run our engine through core_ui (cui_verify.c); the others are greyed with a NOT DECODED / not supported tip.
 * OURS: the stock-removal engine, the viewport sharing the main window's scene and camera, the status line under the slider. */
#include "simwin.h"
#include "icons.h"
extern "C" {
#include "scene.h"
#include "core_ui/core_ui.h"
#include "core_bridge.h"
}
#include "SARibbonBar.h"
#include "SARibbonCategory.h"
#include "SARibbonContextCategory.h"
#include "SARibbonPanel.h"
#include "SARibbonQuickAccessBar.h"
#include "SARibbonApplicationButton.h"
#include <QAction>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cstdio>

static const char *NOT_SUPPORTED = "Not supported in apprenticeCAM (our Verify engine / window) - greyed";
static const char *NOT_DECODED = "NOT DECODED - greyed";

static QIcon ubvsIcon(const QString &base, int sz) { return base.isEmpty() ? QIcon() : Icons::icon("#file:ubvs_" + base + ".png", sz, sz); }

/* ------------------------------------------------------------------------------------------------ viewport (ours) */
SimViewport::SimViewport(ViewApi *main, QWidget *parent) : QWidget(parent), main_(main) {
    in_ = new ViewInput(this, this);                       /* rotate / pan / zoom as the main view (shared camera: ours) */
    setAttribute(Qt::WA_OpaquePaintEvent); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
}
template <class F> void SimViewport::sized(F f) {          /* the view size globals = this viewport while it handles input */
    int sw = view_w, sh = view_h; view_w = width(); view_h = height(); f(); view_w = sw; view_h = sh; update();
}
QImage SimViewport::grabView() { QImage im; qreal r = devicePixelRatioF(); main_->withGL([&] { im = renderSceneImage(width(), height(), r); }); return im; }
void SimViewport::paintEvent(QPaintEvent *) {
    QPainter p(this);
    QImage im = grabView();
    if (im.isNull()) { p.fillRect(rect(), Qt::white); return; }
    p.drawImage(0, 0, im);
}
void SimViewport::mousePressEvent(QMouseEvent *e) { sized([&] { in_->press(e); }); }
void SimViewport::mouseReleaseEvent(QMouseEvent *e) { sized([&] { in_->release(e); }); }
void SimViewport::mouseMoveEvent(QMouseEvent *e) { if (e->buttons()) sized([&] { in_->move(e); }); }
void SimViewport::wheelEvent(QWheelEvent *e) { sized([&] { in_->wheel(e); }); }

/* ------------------------------------------------------------------------------------------------ window */
SimulatorWindow::SimulatorWindow(ViewApi *mainView, QWidget *owner) : SARibbonMainWindow(owner), main_(mainView) {
    setObjectName("verify");
    setWindowFlag(Qt::Window, true);
    setWindowTitle("apprenticeCAM Simulator");               /* resx Window_ApplicationName "Mastercam Simulator" (debranded) */
    resize(1290, 768);                                        /* UBVS.Properties.UbvsPersistant Width 1290 / Height 768 */
    move(200, 200);                                           /* UbvsPersistant Left 200 / Top 200 */
    setRibbonTheme(SARibbonTheme::RibbonThemeOffice2013);
    setStyleSheet(styleSheet() + "\nSARibbonToolButton:disabled { color: rgb(160,160,160); }\n");
    buildRibbon();
    buildWorkspace();
    buildDocks();
}

QAction *SimulatorWindow::act(const QString &id, const QString &label, const QString &icon, int size, const QString &tip, bool live) {
    QAction *a = new QAction(ubvsIcon(icon, size), label, this);
    a->setObjectName(id);
    a->setToolTip(live ? (tip.isEmpty() ? label : tip) : (tip.isEmpty() ? label : tip) + "\n" + (live ? "" : NOT_SUPPORTED));
    a->setEnabled(live);
    acts_.insert(id, a);
    return a;
}
QAction *SimulatorWindow::checkAct(const QString &id, const QString &label, const QString &icon, int size, const QString &tip, bool live, int *flag) {
    QAction *a = act(id, label, icon, size, tip, live);
    a->setCheckable(true);
    if (flag) { flags_.insert(id, flag); connect(a, &QAction::toggled, this, [this, flag](bool on) { if (filling_) return; *flag = on; vf_apply_visibility(); emit changed(); }); }
    return a;
}
QCheckBox *SimulatorWindow::check(SARibbonPanel *p, const QString &id, const QString &label, const QString &tip, int *flag, bool enabled) {
    QCheckBox *c = new QCheckBox(label);
    c->setObjectName(id); c->setEnabled(enabled && flag);
    c->setToolTip(enabled && flag ? (tip.isEmpty() ? label : tip) : (tip.isEmpty() ? label : tip) + "\n" + (flag ? NOT_DECODED : NOT_SUPPORTED));
    if (flag) { flags_.insert(id, flag); connect(c, &QCheckBox::toggled, this, [this, flag](bool on) { if (filling_) return; *flag = on; vf_apply_visibility(); emit changed(); }); }
    checks_.insert(id, c);
    p->addSmallWidget(c);
    return c;
}

/* the ribbon (baml ubvsRibbon): Backstage (File), Home, View, Debug Tools (Visibility DebugUiVisibility: hidden), the
   contextual tab groups Simulation / Backplot / Verify (the one of the current mode shown: Verify) */
void SimulatorWindow::buildRibbon() {
    SARibbonBar *rb = ribbonBar();
    rb->applicationButton()->setText("File");
    /* Backstage: Save Stock as STL, Help tab, Print, Options, Exit (ours: a menu instead of the backstage page) */
    QMenu *bs = new QMenu(this);
    bs->addAction(act("SaveStockAsStlCommand", "Save Stock as STL", "savestl16", 16, "", false));
    bs->addAction(act("HelpCommand", "Help", "help16", 16, "Mastercam Simulator Help / Export Support Data / License Information / About", false));
    bs->addAction(act("PrintCommand", "Print", "", 16, "", false));
    bs->addAction(act("ShowOptionsCommand", "Options", "options16", 16, "The Options window (options.baml) is not ported", false));
    QAction *ex = act("CloseCommand", "Exit", "exit16", 16, "", true);
    connect(ex, &QAction::triggered, this, &QWidget::close);
    bs->addAction(ex);
    connect(rb, &SARibbonBar::applicationButtonClicked, this, [this, bs] { bs->popup(ribbonBar()->applicationButton()->mapToGlobal(QPoint(0, ribbonBar()->applicationButton()->height()))); });

    /* ---- Home (HomeTab) */
    SARibbonCategory *home = rb->addCategoryPage("Home");
    SARibbonPanel *g = home->addPanel("Mode");
    QAction *bp = checkAct("Backplot", "Backplot", "backplot32", 32, "Backplot\nThe Simulator's Backplot mode is not ported (apprenticeCAM's Backplot is the classic one)", false, nullptr);
    g->addLargeAction(bp);
    QAction *vf = checkAct("Verify", "Verify", "verify32", 32, "Verify", true, nullptr);
    vf->setChecked(true);                                     /* IsVerificationEnabled: the Verify session */
    connect(vf, &QAction::triggered, this, [vf] { vf->setChecked(true); });
    g->addLargeAction(vf);
    /* MachineSimulation: Visibility = IsCNCSimVisible (no machine simulation here -> collapsed) */

    g = home->addPanel("Playback");
    /* ToggleIsColorLoopCommand (CanExecute = IsStockEnabled): CustomSettings.IsColorLoop (the cut colours,
       formats/simulator.md 9) */
    g->addLargeAction(checkAct("ColorLoop", "Color Loop", "colorloop32", 32, "Color Loop", true, &vf_set.color_loop));
    QAction *sc = checkAct("StopConditions", "Stop Conditions", "stopon16", 16, "Stop Conditions", true, &vf_set.stop_enabled);
    QMenu *scm = new QMenu(this);
    scm->addAction(checkAct("StopOnOperationChange", "Operation Change", "", 16, "", true, &vf_set.stop_opchange));
    scm->addAction(checkAct("StopOnToolChange", "Tool Change", "", 16, "", true, &vf_set.stop_toolchange));
    scm->addAction(checkAct("StopOnCollision", "Collision", "", 16, "Collision (ModuleWorks collision checking)", false, nullptr));
    scm->addAction(checkAct("StopOnToolInspection", "Tool Inspection", "", 16, "Tool inspection", false, nullptr));
    scm->addSection("XYZ Change");
    scm->addAction(checkAct("StopOnValueChangeX", "X Change", "", 16, "", true, &vf_set.stop_dx));
    scm->addAction(checkAct("StopOnValueChangeY", "Y Change", "", 16, "", true, &vf_set.stop_dy));
    scm->addAction(checkAct("StopOnValueChangeZ", "Z Change", "", 16, "", true, &vf_set.stop_dz));
    scm->addSection("Specific Values");
    scm->addAction(act("StopOnXYZStep", "Specify Values", "", 16, "Specific Values (the XYZValue window is not ported)", false));
    sc->setMenu(scm);
    g->addSmallAction(sc, QToolButton::MenuButtonPopup);
    QAction *cc = act("CollisionChecking", "Collision Checking", "collision16", 16, "Collision Checking (Milling Holder / Shank / Shoulder / Cutting Length, Turning Holder / Insert)", false);
    g->addSmallAction(cc);
    QAction *mc = act("MaterialCutting", "Material Cutting", "materialcutting16", 16, "Material Cutting (Milling Holder / Shank / Shoulder / Cutting Length, Turning Holder / Insert)", false);
    g->addSmallAction(mc);

    g = home->addPanel("Visibility");                          /* GroupToolComponents */
    g->addLargeAction(act("ToolComponentsMenu", "Tool Components", "tool32", 32, "Tool Components (Holder / Shank / Shoulder / Cutting Length)", false));
    g->addSeparator();
    check(g, "ToolpathVisibility", "Toolpath", "", &vf_set.toolpath_vis, true);
    check(g, "ToolVisibility", "Tool", "", &vf_set.tool_vis, true);
    check(g, "WorkpieceVisibility", "Workpiece", "Workpiece (IsEnabled = IsWorkpieceSet: no workpiece is passed)", nullptr, false);
    check(g, "StockVisibility", "Stock", "", &vf_set.stock_vis, true);
    check(g, "InitialStockVisibility", "Initial Stock", "", nullptr, false);
    check(g, "FixtureVisibility", "Fixtures", "", nullptr, false);
    check(g, "WireframeGeometryVisibility", "Wireframe", "", &vf_set.wire_vis, true);
    check(g, "GnomonVisibility", "Gnomon", "", nullptr, false);
    check(g, "CoordianteSystemVisibility", "Axes", "", &vf_set.axes_vis, true);

    g = home->addPanel("Focus");
    g->addLargeAction(checkAct("FocusTool", "Tool", "toolfocus32", 32, "Tool (T)", false, nullptr));
    g->addLargeAction(checkAct("FocusWorkpiece", "Workpiece", "workpiecefocus32", 32, "Workpiece (W)", false, nullptr));
    g->addLargeAction(checkAct("FocusMultipleWorkpiece", "Multiple Workpiece", "multipleworkpiecefocus32", 32, "Multiple Workpiece (M)", false, nullptr));

    g = home->addPanel("Defaults");
    QAction *rd = act("ReadSimulationSettingsXmlCommand", "Read from Defaults", "loaddefaults32", 32, "Read From Defaults", true);
    connect(rd, &QAction::triggered, this, [this] {          /* VM ApplyReadSimulationSettingsXml: an Open dialog, then Deserialize */
        char d[1100]; QString start = QString::fromUtf8(qc_config_file(d, sizeof d, VF_SETTINGS_FILE));
        QString f = QFileDialog::getOpenFileName(this, "Read From Defaults", start);
        if (!f.isEmpty()) { vf_settings_read(f.toUtf8().constData()); vf_apply_visibility(); refresh(); emit changed(); }
    });
    g->addLargeAction(rd);
    g->addLargeAction(act("SaveDefaults", "Save to Defaults", "savedefaults32", 32, "Save To Defaults (ours: the defaults file is not written)", false));
    g->addLargeAction(act("ResetSimulationSettingsXmlCommand", "Reset to Defaults", "resetdefaults32", 32, "Reset To Defaults", false));

    g = home->addPanel("Video");                               /* GroupCapture */
    g->addLargeAction(act("ToggleCaptureButton", "Record", "movierecordingoff32", 32, "", false));
    g->addLargeAction(act("CaptureOptionsPopupButton", "Recording Options", "recordingoptions32", 32, "", false));

    /* ---- View (ViewTab) */
    SARibbonCategory *view = rb->addCategoryPage("View");
    g = view->addPanel("Show");
    g->addLargeAction(act("Button_ShowCollisions_Click", "Collision Report", "gougereport32", 32, "Collision Report (ModuleWorks collision checking)", false));
    QAction *ml = act("Button_ShowMoveList_Click", "Move List", "movelist32", 32, "", true);
    connect(ml, &QAction::triggered, this, [this] { moves_->show(); moves_->raise(); });   /* MW Button_ShowMoveList_Click: twMoveInfo.Activate */
    g->addLargeAction(ml);
    g = view->addPanel("3D View");
    auto viewBtn = [&](const char *id, const char *label, const char *icon, int sz, const char *tip, int std) {
        QAction *a = act(id, label, icon, sz, tip, true);
        connect(a, &QAction::triggered, this, [this, std] {
            int sw = view_w, sh = view_h; view_w = vp_->width(); view_h = vp_->height();
            main_->withGL([std] { if (std) view_std(std); view_fit(0); });   /* ours: our view_std / view_fit on the shared camera */
            view_w = sw; view_h = sh; emit changed(); vp_->update(); });
        return a; };
    g->addLargeAction(viewBtn("ViewFitToScreenCommand", "Fit", "fit32", 32, "Fit", 0));
    g->addLargeAction(viewBtn("ViewIsometricCommand", "Isometric", "3dviewisometric32", 32, "Isometric (Alt+7)", 7));
    g->addSmallAction(viewBtn("ViewTopCommand", "Top", "3dviewtop16", 16, "Top (Alt+1)", 1));
    g->addSmallAction(viewBtn("ViewRightCommand", "Right", "3dviewright16", 16, "Right (Alt+5)", 5));
    QAction *fr = viewBtn("ViewFrontCommand", "Front", "3dviewfront16", 16, "Front (Alt+2)", 2);
    QMenu *frm = new QMenu(this);
    frm->addAction(viewBtn("ViewBottomCommand", "Bottom", "3dviewbottom16", 16, "Bottom (Alt+4)", 4));
    frm->addAction(viewBtn("ViewLeftCommand", "Left", "3dviewleft16", 16, "Left (Alt+6)", 6));
    frm->addAction(viewBtn("ViewBackCommand", "Back", "3dviewback16", 16, "Back (Alt+3)", 3));
    fr->setMenu(frm);
    g->addSmallAction(fr, QToolButton::MenuButtonPopup);
    g = view->addPanel("Viewports");
    QAction *sv = checkAct("ViewportIdSingleView", "Single View", "viewportsingleview32", 32, "Single View", true, nullptr);
    sv->setChecked(true); connect(sv, &QAction::triggered, this, [sv] { sv->setChecked(true); });   /* CustomSettings Viewports SingleView */
    g->addLargeAction(sv);
    g->addLargeAction(checkAct("Viewport2ViewsRow", "2 Views in Rows", "viewporttwoviewsinrows32", 32, "2 Views in Rows", false, nullptr));
    g->addLargeAction(checkAct("Viewport2ViewsColumn", "2 Views in Columns", "viewporttwoviewsincolumns32", 32, "2 Views in Columns", false, nullptr));
    g->addLargeAction(checkAct("Viewport4Views", "4 Views", "viewportfourviews32", 32, "4 Views", false, nullptr));
    g->addLargeAction(act("ViewportRestore", "Restore default view", "viewportrestore32", 32, "Restore default view", false));
    g = view->addPanel("Utilities");
    g->addSmallAction(checkAct("MotionControllerSelection", "Motion Controller", "motioncontroller", 16, "Motion Controller", false, nullptr));
    g->addLargeAction(checkAct("SyncViews", "Sync views", "syncview32", 32, "Sync views", false, nullptr));

    /* ---- the Verify contextual tab (VerifyContextualTab / VerifyTab) */
    verifyCtx_ = rb->addContextCategory("Verify", QColor(), "Verify");
    SARibbonCategory *vt = verifyCtx_->addCategoryPage("Verify");
    g = vt->addPanel("Operations");
    g->addSmallAction(checkAct("VerifyAllOperations", "All Operations", "alloperations32", 16, "All Operations", false, nullptr));
    g->addSmallAction(checkAct("VerifyCurrentOperation", "Current Operation", "currentoperation32", 16, "Current Operation", false, nullptr));
    g->addSmallAction(checkAct("VerifySegment", "Segment", "segment32", 16, "Segment", false, nullptr));
    g = vt->addPanel("Toolpath");
    g->addSmallAction(checkAct("VerifyTPTrace", "Trace", "trace32", 16, "Trace", false, nullptr));
    g->addSmallAction(checkAct("VerifyTPFollow", "Follow", "follow32", 16, "Follow", false, nullptr));
    g->addSmallAction(checkAct("VerifyTPComplete", "Both", "tracefollow32", 16, "Both", false, nullptr));
    g = vt->addPanel("Display");
    g->addLargeAction(act("VerifyEnableRestrictDrawing", "Restrict Drawing", "restrictdrawing32", 32, "", false));
    g->addSmallAction(act("VerifyDisableRestrictDrawing", "Stop Restrict Drawing", "stoprestrictdrawing32", 16, "", false));
    g->addSmallAction(checkAct("VerifyShowPoints", "Points", "endpoints16", 16, "Points", false, nullptr));
    g->addSmallAction(checkAct("VerifyShowRapidMoves", "Leads", "leads16", 16, "Leads", false, nullptr));
    g = vt->addPanel("Vectors");
    g->addSmallAction(checkAct("VerifyShowVectors", "Enable Vectors", "vectors16", 16, "Enable Vectors", false, nullptr));
    g = vt->addPanel("Analyze");
    {   /* ShowStockCompareCommand: ShowStockCompare = !ShowStockCompare -> the Compare tool window (twStockCompare) shown / closed;
           enabled with IsVerificationEnabledAndStockSet */
        QAction *c = checkAct("ShowStockCompareCommand", "Compare", "stlcompare32", 32, "Compare\nThe Compare tool window: the stock against a model (the deviation is ours - ModuleWorks is not ported)", true, nullptr);
        flags_.insert("ShowStockCompareCommand", &vf_cmp_open);
        connect(c, &QAction::toggled, this, [this](bool on) { if (filling_) return;
            main_->withGL([on] { vf_cmp_show(on); vf_frame_still(); });
            refresh(); emit changed(); });
        g->addLargeAction(c);
    }
    g->addSmallAction(checkAct("KeepPart", "Keep Chips", "keeppart32", 16, "Keep Chips", false, nullptr));
    g->addSmallAction(checkAct("DeletePart", "Remove Chips", "deletepart32", 16, "Remove Chips", false, nullptr));
    g->addSmallAction(checkAct("MeasurePoints", "Points", "point16", 16, "Points", false, nullptr));
    g->addSmallAction(checkAct("MeasureDistances", "Distance", "distances16", 16, "Distance", false, nullptr));
    g->addSmallAction(act("ClearSelectionCommand", "Clear All", "clearseletion16", 16, "Clear All", false));
    g = vt->addPanel("Clipping");
    g->addLargeAction(act("Clipping34", "3/4", "crosssection32", 32, "3/4 (1st..4th quadrant / Off)", false));
    g->addLargeAction(act("ClippingXY", "XY Clipping Plane", "xyclippingplane32", 32, "XY Clipping Plane", false));
    g->addLargeAction(act("ClippingYZ", "YZ Clipping Plane", "yzclippingplane32", 32, "YZ Clipping Plane", false));
    g->addLargeAction(act("ClippingZX", "ZX Clipping Plane", "zxclippingplane32", 32, "ZX Clipping Plane", false));
    g = vt->addPanel("Quality");
    g->addLargeAction(act("VerifierZoomButton", "Accurate Zoom", "applyrefine32", 32, "", false));
    g->addLargeAction(act("ResetVerifierZoomButton", "Reset Zoom", "resetrefine32", 32, "", false));
    g->addSmallAction(checkAct("ToggleShowEdgesCommand", "Show Edges", "showedges16", 16, "Show Edges", false, nullptr));
    g->addSmallAction(checkAct("ToggleFastRenderingCommand", "Turbo Mode", "fastrender16", 16, "Turbo Mode", false, nullptr));
    g->addSmallAction(checkAct("ToggleTrueThreadingCommand", "True Thread", "truethread16", 16, "True Thread", false, nullptr));
    rb->showContextCategory(verifyCtx_);
    rb->raiseCategory(home);

    /* Quick Access Toolbar: CustomSettings QuickAccessToolbarItems = Backplot, Verify, Simulation (collapsed) */
    SARibbonQuickAccessBar *q = rb->quickAccessBar();
    q->addAction(acts_.value("Backplot")); q->addAction(acts_.value("Verify"));
}

/* the workspace (baml Name="workspace"): Grid rows *, 22, Auto = the viewport (white), the progress bar + position slider
   (Background #F7F7F7, top border #CCC), the playback bar (gradient #FFFFFF / #F1F1F1 / #E1E1E1 / #F3F3F3, columns 1* 2* 1*) */
void SimulatorWindow::buildWorkspace() {
    QWidget *ws = new QWidget(this);
    QVBoxLayout *v = new QVBoxLayout(ws); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(0);
    vp_ = new SimViewport(main_, ws);
    v->addWidget(vp_, 1);
    QWidget *row = new QWidget(ws); row->setFixedHeight(22);
    row->setStyleSheet("background: #F7F7F7; border-top: 1px solid #CCC;");
    QHBoxLayout *rh = new QHBoxLayout(row); rh->setContentsMargins(4, 0, 4, 0);
    pos_ = new QSlider(Qt::Horizontal, row);                  /* Slider Minimum 1, Maximum OverallToolpathLength, Value SliderValueOneBased */
    pos_->setMinimum(1); pos_->setTickPosition(QSlider::TicksBelow);
    connect(pos_, &QSlider::valueChanged, this, [this](int val) { if (filling_) return; main_->withGL([val] { vf_slider_set(val); vf_frame_still(); }); emit changed(); });
    rh->addWidget(pos_);
    v->addWidget(row);
    QWidget *bar = new QWidget(ws);
    bar->setObjectName("simbar");
    bar->setStyleSheet("#simbar { border-top: 1px solid #CCC; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:0.5 #F1F1F1, stop:0.5 #E1E1E1, stop:1 #F3F3F3); }");
    QHBoxLayout *bh = new QHBoxLayout(bar); bh->setContentsMargins(5, 4, 5, 4);
    QWidget *left = new QWidget(bar), *mid = new QWidget(bar), *right = new QWidget(bar);
    QHBoxLayout *lh = new QHBoxLayout(left); lh->setContentsMargins(0, 0, 0, 0);
    QLabel *pl = new QLabel("Precision: ", left); pl->setEnabled(false);
    prec_ = new QSlider(Qt::Horizontal, left); prec_->setFixedWidth(100); prec_->setRange(0, 10); prec_->setEnabled(false);
    prec_->setTickPosition(QSlider::TicksBelow); prec_->setTickInterval(1);
    prec_->setToolTip(QString("Precision (VerifySettings PrecisionIndex): the ModuleWorks model precision\n") + NOT_SUPPORTED);
    lh->addWidget(pl); lh->addWidget(prec_); lh->addStretch(1);
    QHBoxLayout *mh = new QHBoxLayout(mid); mh->setContentsMargins(10, 0, 10, 0); mh->setSpacing(4); mh->addStretch(1);
    for (int k = 0; k < 8; k++) {                             /* GlassButton 32 x 32 (Play 48 x 48), Background #FF1D5BBA, 16 px images */
        QToolButton *b = new QToolButton(mid);
        int big = VF_BTNS[k].id == 1; b->setFixedSize(big ? 48 : 32, big ? 48 : 32);
        b->setIcon(ubvsIcon(QString(VF_BTNS[k].img).mid(5), 16)); b->setIconSize(QSize(big ? 24 : 16, big ? 24 : 16));
        b->setToolTip(VF_BTNS[k].tip); b->setCheckable(VF_BTNS[k].id == 9);
        b->setStyleSheet("QToolButton { border-radius: 4px; border: 1px solid #0E3E8A; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4A82D6, stop:0.5 #1D5BBA, stop:1 #164C9C); }"
                         "QToolButton:checked { background: #0E3E8A; } QToolButton:hover { background: #3A75D0; }");
        int id = VF_BTNS[k].id;
        connect(b, &QToolButton::clicked, this, [this, id] { run(id); });
        vcr_[k] = b; mh->addWidget(b);
    }
    mh->addStretch(1);
    QHBoxLayout *gh = new QHBoxLayout(right); gh->setContentsMargins(0, 0, 0, 0); gh->addStretch(1);
    gh->addWidget(new QLabel("Speed: ", right));
    speed_ = new QSlider(Qt::Horizontal, right); speed_->setFixedWidth(100);   /* Value PerformanceIndex, Min/Max = Regulator 0 / 20, TickFrequency 2 */
    speed_->setRange(VF_SPEED_MIN, VF_SPEED_MAX); speed_->setTickPosition(QSlider::TicksBelow); speed_->setTickInterval(2);
    connect(speed_, &QSlider::valueChanged, this, [this](int val) { if (filling_) return; vf_set.speed = val; emit changed(); });
    gh->addWidget(speed_);
    bh->addWidget(left, 1); bh->addWidget(mid, 2); bh->addWidget(right, 1);
    v->addWidget(bar);
    status_ = new QLabel(ws);                                  /* ours: the engine's status (removed volume, rapid warnings) */
    status_->setContentsMargins(6, 2, 6, 2);
    status_->setToolTip("apprenticeCAM's Verify engine (ours)");
    v->addWidget(status_);
    setCentralWidget(ws);
}

/* the tool windows (baml Docking.SplitContainer right of the workspace): Move List (twMoveInfo, a property grid), Collision
   Report (twCollisions, columns Move / Collision), Compare (twStockCompare, IsOpen False), Axis Control (CNC sim only) */
void SimulatorWindow::buildDocks() {
    moves_ = new QDockWidget("Move List", this); moves_->setObjectName("twMoveInfo");
    grid_ = new QTableWidget(0, 2, moves_);
    grid_->horizontalHeader()->setVisible(false); grid_->verticalHeader()->setVisible(false);
    grid_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    grid_->horizontalHeader()->setStretchLastSection(true);
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers); grid_->setSelectionMode(QAbstractItemView::NoSelection);
    grid_->verticalHeader()->setDefaultSectionSize(20);
    moves_->setWidget(grid_);
    addDockWidget(Qt::RightDockWidgetArea, moves_);
    coll_ = new QDockWidget("Collision Report", this); coll_->setObjectName("twCollisions");
    QTableWidget *ct = new QTableWidget(0, 2, coll_);
    ct->setHorizontalHeaderLabels({ "Move", "Collision" }); ct->verticalHeader()->setVisible(false); ct->setEnabled(false);
    ct->setToolTip(QString("Collision Report: ModuleWorks collision checking\n") + NOT_SUPPORTED);
    coll_->setWidget(ct);
    addDockWidget(Qt::RightDockWidgetArea, coll_);
    tabifyDockWidget(coll_, moves_);                           /* one StylingToolWindowContainer: tabs Move List / Collision Report */
    moves_->raise();
    resizeDocks({ moves_ }, { 300 }, Qt::Horizontal);          /* ours: the width (the docking layout's width is not in the baml) */
    buildCompare();
}

/* the Compare tool window (baml twStockCompare, IsOpen False; formats/simulator.md 8): row 0 (TopRowButtons) = the Stock to
   leave image + DoubleEditBox (StockToLeave), Refresh (RefreshCommand), Show Stock (ToggleStockVisibilityCommand), Show Model
   (ToggleStlCompareVisibilityCommand), Compare Options (ShowStockCompareOptionsCommand); row 1 the target combo
   (StockCompareComboBoxSource: [Workpiece,] Browse..., the files browsed); row 2 the colour grid (colour, Start, End;
   StockCompareColors, single full-row selection); row 3 an empty button row. Background #F0F0F0. */
void SimulatorWindow::buildCompare() {
    cmp_ = new QDockWidget("Compare", this); cmp_->setObjectName("twStockCompare");   /* resx CompareToolWindow_Title */
    QWidget *w = new QWidget(cmp_); w->setStyleSheet("background: #F0F0F0;");
    QVBoxLayout *v = new QVBoxLayout(w); v->setContentsMargins(3, 2, 3, 2); v->setSpacing(4);
    QHBoxLayout *r0 = new QHBoxLayout; r0->setSpacing(3);
    QLabel *img = new QLabel(w); img->setPixmap(ubvsIcon("stocktoleave16", 16).pixmap(16, 16)); img->setToolTip("Stock to leave");   /* CompareToolWindowStockToLeaveTextBlock_Text */
    cstl_ = new QDoubleSpinBox(w); cstl_->setObjectName("StockToLeave"); cstl_->setDecimals(4); cstl_->setRange(-1e6, 1e6); cstl_->setMinimumWidth(20);
    cstl_->setButtonSymbols(QAbstractSpinBox::NoButtons); cstl_->setToolTip("Stock to leave");
    connect(cstl_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double x) { if (filling_) return; vf_cmp_set_stl(x); refresh(); });
    auto tb = [&](const QString &id, const QString &label, const QString &icon) {
        QToolButton *b = new QToolButton(w); b->setObjectName(id); b->setText(label); b->setToolTip(label);
        b->setIcon(ubvsIcon(icon, 16)); b->setToolButtonStyle(Qt::ToolButtonIconOnly); b->setAutoRaise(true); return b; };
    crefresh_ = tb("RefreshCommand", "Refresh", "refresh16");                       /* CompareToolWindowRefreshButton_Label */
    connect(crefresh_, &QToolButton::clicked, this, [this] { main_->withGL([] { vf_cmp_refresh(); vf_frame_still(); }); refresh(); emit changed(); });
    cstock_ = tb("ToggleStockVisibilityCommand", "Show Stock", "showstock16"); cstock_->setCheckable(true);   /* CompareToolWindowShowStockButton_Label */
    connect(cstock_, &QToolButton::toggled, this, [this](bool on) { if (filling_) return; vf_set.stock_vis = on; vf_apply_visibility(); main_->withGL([] { vf_frame_still(); }); refresh(); emit changed(); });
    QToolButton *model = tb("ToggleStlCompareVisibilityCommand", "Show Model", "showmodel16");                 /* CompareToolWindowShowModelButton_Label */
    model->setCheckable(true); model->setEnabled(false);
    model->setToolTip(QString("Show Model\nThe workpiece / STL model is not drawn in our viewport\n") + NOT_SUPPORTED);
    QToolButton *opt = tb("ShowStockCompareOptionsCommand", "Compare Options", "options16");                   /* StockCompareDynamicRangesCommandButton_Label */
    connect(opt, &QToolButton::clicked, this, [this] { compareOptions(); });
    r0->addWidget(img); r0->addWidget(cstl_, 1); r0->addWidget(crefresh_); r0->addWidget(cstock_); r0->addWidget(model); r0->addWidget(opt);
    v->addLayout(r0);
    csrc_ = new QComboBox(w); csrc_->setObjectName("StockCompareComboBox");
    connect(csrc_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        if (filling_ || i < 0 || i >= vf_cmp_nitems) return;
        if (vf_cmp_items[i].kind == VF_CMP_BROWSE) {         /* SetStockCompareStl: OpenFileDialog Title / Filter (Resources) */
            QString f = QFileDialog::getOpenFileName(this, "Open STL model for the stock comparison", QString(), "STL files(.stl) (*.stl);;All Files (*.*) (*)");
            if (!f.isEmpty()) { QByteArray b = f.toLocal8Bit(); main_->withGL([&] { vf_cmp_add_stl(b.constData()); vf_frame_still(); }); }
        } else main_->withGL([i] { vf_cmp_select(i); vf_frame_still(); });
        refresh(); emit changed(); });
    v->addWidget(csrc_);
    cgrid_ = new QTableWidget(0, 3, w); cgrid_->setObjectName("StockCompareColors");
    cgrid_->setHorizontalHeaderLabels({ "", "Start", "End" });   /* CompareToolWindowColorGridStart_Header / _End_Header */
    cgrid_->verticalHeader()->setVisible(false);
    cgrid_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    cgrid_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch); cgrid_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    cgrid_->setSelectionBehavior(QAbstractItemView::SelectRows); cgrid_->setSelectionMode(QAbstractItemView::SingleSelection);
    cgrid_->setStyleSheet("background: white;");
    connect(cgrid_, &QTableWidget::cellChanged, this, [this](int r, int c) {
        if (filling_ || c == 0) return;
        bool ok = false; double x = cgrid_->item(r, c)->text().toDouble(&ok);    /* float.TryParse */
        if (ok) vf_cmp_set_value(r, c == 1, x);
        fillCompare(); refresh(); });
    connect(cgrid_, &QTableWidget::cellDoubleClicked, this, [this](int r, int c) {
        if (c != 0) return;
        char a[32], b[32]; unsigned char rgb[3]; vf_cmp_row(r, a, sizeof a, b, sizeof b, rgb);
        QColor q = QColorDialog::getColor(QColor(rgb[0], rgb[1], rgb[2]), this);   /* MyDataGridColorColumn (Actipro colour editor; ours: QColorDialog) */
        if (!q.isValid()) return;
        unsigned char n[3] = { (unsigned char)q.red(), (unsigned char)q.green(), (unsigned char)q.blue() };
        vf_cmp_set_color(r, n); fillCompare(); refresh(); });
    v->addWidget(cgrid_, 1);
    cmp_->setWidget(w);
    addDockWidget(Qt::RightDockWidgetArea, cmp_);
    tabifyDockWidget(moves_, cmp_);
    cmp_->hide();                                                /* IsOpen False */
    connect(cmp_, &QDockWidget::visibilityChanged, this, [this](bool) { if (filling_) return;
        bool on = !cmp_->isHidden(); if (on != (vf_cmp_open != 0)) { main_->withGL([on] { vf_cmp_show(on); vf_frame_still(); }); refresh(); emit changed(); } });
    moves_->raise();
}
void SimulatorWindow::fillCompare() {
    bool was = filling_; filling_ = true;
    csrc_->clear();
    for (int i = 0; i < vf_cmp_nitems; i++) csrc_->addItem(vf_cmp_items[i].kind == VF_CMP_BROWSE ? QString("Browse...")
        : vf_cmp_items[i].kind == VF_CMP_WORKPIECE ? QString("Workpiece") : QString::fromLocal8Bit(vf_cmp_items[i].path));   /* DisplayMemberPath Content */
    csrc_->setCurrentIndex(vf_cmp_sel);
    cstl_->setValue(vf_set.stock_to_leave);
    crefresh_->setEnabled(vf_cmp_can_refresh() != 0);
    cstock_->setChecked(vf_set.stock_vis != 0);
    cgrid_->setRowCount(vf_set.cmp_n);
    for (int r = 0; r < vf_set.cmp_n; r++) {
        char a[32], b[32]; unsigned char rgb[3]; vf_cmp_row(r, a, sizeof a, b, sizeof b, rgb);
        QTableWidgetItem *c0 = new QTableWidgetItem; c0->setBackground(QColor(rgb[0], rgb[1], rgb[2])); c0->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        c0->setSizeHint(QSize(28, 18));
        QTableWidgetItem *c1 = new QTableWidgetItem(a), *c2 = new QTableWidgetItem(b);
        if (!strcmp(a, "Min")) c1->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);   /* IsStartValueReadOnly / IsEndValueReadOnly */
        if (!strcmp(b, "Max")) c2->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        cgrid_->setItem(r, 0, c0); cgrid_->setItem(r, 1, c1); cgrid_->setItem(r, 2, c2);
    }
    filling_ = was;
}
/* compare.xaml (UBVS.Compare, NoResize, SizeToContent, CenterOwner): "Tolerance" + DoubleEditBox (Minimum 0, Format G7, Width 125),
   Help / OK / Cancel (Width 70); OK = CompareWindowViewModel.ApplyOk (Tolerance 0 at open; > 0 rebuilds the ranges) */
void SimulatorWindow::compareOptions() {
    QDialog d(this); d.setObjectName("compareOptions"); d.setWindowTitle("Compare Options");   /* CompareXaml Window_Title */
    d.setStyleSheet("background: #F0F0F0;");
    QVBoxLayout *v = new QVBoxLayout(&d); v->setContentsMargins(12, 12, 12, 12);
    QHBoxLayout *r = new QHBoxLayout; r->addWidget(new QLabel("Tolerance:", &d)); r->addSpacing(20);   /* CompareXaml LabelTolerance_Content */
    QDoubleSpinBox *t = new QDoubleSpinBox(&d); t->setObjectName("Tolerance"); t->setDecimals(7); t->setMinimum(0); t->setMaximum(1e6); t->setValue(0); t->setFixedWidth(125);
    r->addWidget(t); r->addStretch(1); v->addLayout(r);
    QHBoxLayout *b = new QHBoxLayout;
    QPushButton *help = new QPushButton("Help", &d); help->setFixedWidth(70); help->setEnabled(false); help->setToolTip(NOT_SUPPORTED);
    QPushButton *ok = new QPushButton("OK", &d), *cancel = new QPushButton("Cancel", &d); ok->setFixedWidth(70); cancel->setFixedWidth(70);
    b->addWidget(help); b->addStretch(1); b->addWidget(ok); b->addWidget(cancel); v->addLayout(b);
    connect(ok, &QPushButton::clicked, &d, &QDialog::accept); connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    if (d.exec() == QDialog::Accepted) { vf_cmp_options(t->value()); fillCompare(); refresh(); }
}

void SimulatorWindow::run(int btn) {
    main_->withGL([btn] { vf_btn(btn); vf_frame_still(); });
    emit changed();
}

void SimulatorWindow::refresh() {
    if (!vf_on) return;
    filling_ = true;
    pos_->setMaximum(vfnm > 1 ? vfnm : 1);                     /* TickFrequency = OverallToolpathLength * 0.1 */
    pos_->setTickInterval(vfnm / 10 > 0 ? vfnm / 10 : 1);
    pos_->setValue(vf_pos < 1 ? 1 : vf_pos);
    speed_->setValue(vf_set.speed);
    prec_->setValue(vf_set.precision);
    for (int k = 0; k < 8; k++) {
        if (VF_BTNS[k].id == 1) vcr_[k]->setIcon(ubvsIcon(vf_run ? "ubvsstop16" : "ubvsplay16", 16));   /* the Play image: UBVSPlay16 / UBVSStop16 */
        if (VF_BTNS[k].id == 9) vcr_[k]->setChecked(vf_set.loop != 0);
    }
    for (auto it = flags_.begin(); it != flags_.end(); ++it) {
        if (QAction *a = acts_.value(it.key())) a->setChecked(*it.value() != 0);
        if (QCheckBox *c = checks_.value(it.key())) c->setChecked(*it.value() != 0);
    }
    char rows[VF_MI_N][2][128]; int n = vf_moveinfo(rows);
    grid_->setRowCount(n);
    for (int i = 0; i < n; i++) {
        for (int c = 0; c < 2; c++) {
            QTableWidgetItem *it = grid_->item(i, c);
            if (!it) { it = new QTableWidgetItem; grid_->setItem(i, c, it); }
            it->setText(QString::fromUtf8(rows[i][c]));
        }
    }
    if (cmp_) {                                                  /* ShowStockCompareToolWindow: shown and brought to the front */
        cmp_->setVisible(vf_cmp_open != 0);
        if (vf_cmp_open && !cmpShown_) { tabifyDockWidget(moves_, cmp_); cmp_->raise(); }   /* tabified again: on top of the group (ours) */
        cmpShown_ = vf_cmp_open != 0;
        fillCompare();
    }
    char a[300], b[300]; int warn = vf_status(a, sizeof a, b, sizeof b);
    status_->setText(QString::fromUtf8(b));
    status_->setStyleSheet(warn ? "color: rgb(180,30,30);" : "color: rgb(25,25,25);");
    filling_ = false;
    vp_->update();
}

void SimulatorWindow::closeEvent(QCloseEvent *e) {             /* CloseCommand / the window's close box: the session ends */
    main_->withGL([] { vf_stop(); });
    emit closedByUser(); emit changed();
    e->accept();
}
void SimulatorWindow::keyPressEvent(QKeyEvent *e) {
    int k = 0;
    if (!e->modifiers() || e->modifiers() == Qt::KeypadModifier) switch (e->key()) {
        case Qt::Key_S: k = 'S'; break; case Qt::Key_B: k = 'B'; break; case Qt::Key_R: k = 'R'; break;
        case Qt::Key_N: k = 'N'; break; case Qt::Key_P: k = 'P'; break;
        case Qt::Key_Home: k = VF_KEY_HOME; break; case Qt::Key_Space: k = VF_KEY_SPACE; break;
        case Qt::Key_Plus: k = '+'; break; case Qt::Key_Minus: k = '-'; break;
        default: break; }
    if (!k) { SARibbonMainWindow::keyPressEvent(e); return; }
    main_->withGL([k] { vf_keys(k); vf_frame_still(); });
    emit changed();
}

void SimulatorWindow::dump() const {
    printf("    simulator: %s, mode Verify, pos %d/%d, run %d, speed %d (%.1f moves/s, %.2f frames/s), loop %d, stop bits %d, settings %s\n",
           windowTitle().toUtf8().constData(), vf_pos, vfnm, vf_run, vf_set.speed, vf_moves_per_second(), vf_frames_per_second(),
           vf_set.loop, vf_last_stop, vf_set.source);
    QStringList on, off;
    for (auto it = acts_.begin(); it != acts_.end(); ++it) (it.value()->isEnabled() ? on : off) << it.key() + (it.value()->isCheckable() ? (it.value()->isChecked() ? "[x]" : "[ ]") : "");
    for (auto it = checks_.begin(); it != checks_.end(); ++it) (it.value()->isEnabled() ? on : off) << it.key() + (it.value()->isChecked() ? "[x]" : "[ ]");
    on.sort(); off.sort();
    printf("    simulator live: %s\n    simulator greyed: %s\n", on.join(' ').toUtf8().constData(), off.join(' ').toUtf8().constData());
    for (int i = 0; i < grid_->rowCount(); i++) printf("    simulator move list %s = %s\n", grid_->item(i, 0)->text().toUtf8().constData(), grid_->item(i, 1)->text().toUtf8().constData());
    if (cmp_ && !cmp_->isHidden()) {
        QStringList it; for (int i = 0; i < csrc_->count(); i++) it << csrc_->itemText(i);
        printf("    simulator compare: stock to leave %g, refresh %s, items [%s] current %d\n", cstl_->value(), crefresh_->isEnabled() ? "enabled" : "disabled", it.join(" | ").toUtf8().constData(), csrc_->currentIndex());
        for (int r = 0; r < cgrid_->rowCount(); r++) printf("    simulator compare row %d %s %s .. %s\n", r, cgrid_->item(r, 0)->background().color().name().toUtf8().constData(),
               cgrid_->item(r, 1)->text().toUtf8().constData(), cgrid_->item(r, 2)->text().toUtf8().constData());
    }
}
bool SimulatorWindow::testSet(const QString &id, const QString &v) {
    refresh();                                                /* the controls show vf_* first (no frame between script steps) */
    if (QCheckBox *c = checks_.value(id)) { if (!c->isEnabled()) return false; c->setChecked(v.toInt() != 0); return true; }
    if (QAction *a = acts_.value(id)) {
        if (!a->isEnabled()) return false;
        if (a->isCheckable() && !v.isEmpty()) a->setChecked(v.toInt() != 0); else a->trigger();
        return true;
    }
    if (id == "speed") { speed_->setValue(v.toInt()); return true; }
    if (cmp_) {                                                  /* the Compare tool window's controls by their object names */
        if (QToolButton *b = cmp_->findChild<QToolButton *>(id)) { if (!b->isEnabled()) return false;
            if (b->isCheckable() && !v.isEmpty()) b->setChecked(v.toInt() != 0); else b->click(); return true; }
        if (QDoubleSpinBox *d = cmp_->findChild<QDoubleSpinBox *>(id)) { d->setValue(v.toDouble()); return true; }
        if (id == "StockCompareComboBox") {                      /* an index, or a file path for Browse... (ours: no file dialog in tests) */
            bool ok; int i = v.toInt(&ok);
            if (ok) emit csrc_->activated(i); else { QByteArray b = v.toLocal8Bit(); main_->withGL([&] { vf_cmp_add_stl(b.constData()); vf_frame_still(); }); refresh(); }
            return true; }
        if (id.startsWith("StockCompareColors:")) {              /* StockCompareColors:ROW,COL = text (COL 1 Start, 2 End) */
            QStringList rc = id.mid(19).split(','); if (rc.size() != 2) return false;
            QTableWidgetItem *it = cgrid_->item(rc[0].toInt(), rc[1].toInt()); if (!it || !(it->flags() & Qt::ItemIsEditable)) return false;
            it->setText(v); return true; }
    }
    if (id == "slider") { pos_->setValue(v.toInt()); return true; }
    for (int k = 0; k < 8; k++) if (id == VF_BTNS[k].tip || id == QString::number(VF_BTNS[k].id)) { vcr_[k]->click(); return true; }
    return false;
}
QImage SimulatorWindow::grabWindow() { refresh(); return grab().toImage(); }
