#include "retire.h"
#include "textfit.h"
#include <QScrollArea>
#include "opdlg.h"
#include "contrast.h"
#include "icons.h"
#include <QTreeWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QRadioButton>
#include <QComboBox>
#include <QGroupBox>
#include <QFrame>
#include <QMenu>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QListWidget>
#include <QSlider>
#include <QSignalBlocker>
#include <QProgressDialog>
#include <QThread>
#include <QTimer>
#include <QEventLoop>
#include <QApplication>
#include <atomic>
extern "C" {
#include "mcce_dyn.h"
}
#include "regenprogress.h"
#include <QMouseEvent>
#include "core_ui/cui_opfields.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

/* the templates' font (MS Shell Dlg 8 = 11 px at 96 dpi) and base units 6 x 13 px (chainDu, phase 4) */
static void dlgFont(QWidget *w) { w->setFont(TextFit::dialogFont(w->font())); }
static QRect du(int x, int y, int w, int h) { return chainDu(nullptr, x, y, w, h); }
static int duX(double x) { return (int)lround(x * 6 / 4); }
static int duY(double y) { return (int)lround(y * 13 / 8); }
/* greyed / coloured text, kept readable on the theme's background (contrast.h) */
static QColor bgOf(const QWidget *w, QPalette::ColorRole r = QPalette::Window) { return w ? w->palette().color(r) : QColor(255, 255, 255); }
static QColor greyOn(const QWidget *w, QPalette::ColorRole r = QPalette::Window) { return readableOn(QColor(160, 160, 160), bgOf(w, r), 3.0); }
static QString textCss(const QWidget *w, const QColor &c) { return cssColor(readableOn(c, bgOf(w), 4.5)); }
template <class T> static T *mk(QWidget *parent, const QRect &r) { T *w = new T(parent); dlgFont(w); w->setGeometry(r); w->show(); return w; }
/* a picture at its own size with its top left at r (MCUIBitmap / UIPNGImage statics are sized to the bitmap); no image
   in the pack: a blank frame of the rectangle (mcamui pp_pic) */
static QLabel *picLabel(QWidget *parent, const QRect &r, const QString &file) {
    auto *l = new QLabel(parent);
    QPixmap pm = Icons::image("#file:" + file);
    if (pm.isNull()) { l->setGeometry(r); l->setStyleSheet("border: 1px solid rgb(205,205,205);"); }
    else { l->setPixmap(pm); l->setGeometry(r.x(), r.y(), pm.width(), pm.height()); }
    l->show(); return l;
}

/* ------------------------------------------------------------------------------------------------ the tool lists */
/* the CMillToolListCtrl lists outside the Tool Manager (Details view, columns >= 2 - columns 0 / 1 are the icon views' text and
   the image), selected row rgb(51,153,255) as mcamui otp_list: list L's tools IDX[0..N) (core_ui: filter applied, sorted) in
   the columns COLS / widths W of the list's own column set S, whose sort column is marked; the row of tool CUR selected.
   The Tool page (tp_rows / tp_list_cols), the Tool List 6410 (tl6410_rows / tl6410_cols), Edit Common Parameters' tool list
   (ecp_tool_rows / ecp_tool_cols); a header click = SortList on that set (N13-32: each one decoded, see core_ui) */
void fillToolRows(QTableWidget *t, int L, const int *idx, int n, int cur, const int *cols, const int *w, int nc, const TmColSet *s) {
    t->clear(); t->setColumnCount(nc); t->setRowCount(n);
    QStringList hd; for (int k = 0; k < nc; k++) { hd << QString::fromUtf8(tm_head[cols[k]]); t->setColumnWidth(k, w[k]); }
    t->setHorizontalHeaderLabels(hd);
    int sk = -1; for (int k = 0; k < nc; k++) if (cols[k] == s->sortcol) sk = k;
    t->horizontalHeader()->setSortIndicatorShown(sk >= 0);
    if (sk >= 0) t->horizontalHeader()->setSortIndicator(sk, s->sortasc ? Qt::AscendingOrder : Qt::DescendingOrder);
    int sel = -1;
    for (int i = 0; i < n; i++) { if (idx[i] == cur) sel = i; for (int k = 0; k < nc; k++) {
        char c[160]; tm_cell(tm_tool(L, idx[i]), cols[k], c, sizeof c);
        auto *it = new QTableWidgetItem(QString::fromUtf8(c)); it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); t->setItem(i, k, it);
    } }
    for (int i = 0; i < n; i++) t->setRowHeight(i, 18);
    TextFit::fitColumns(t);                           /* ours, user decision UI-2: columns fit their header / cells */
    if (sel >= 0) t->selectRow(sel); else t->clearSelection();
}
/* batch 12: the Tool page's list - the part tools IDX[0..N) (core_ui tp_rows: Filter Active applied, sorted), the row of tool CUR
   selected; the page's own column set (core_ui tp_list_cols / tp_cs, cui_toolpage.c) */
static void fillToolRows(QTableWidget *t, const int *idx, int n, int cur, QVector<int> *colsOut) {
    int cols[TM_COLS], w[TM_COLS], nc = tp_list_cols(cols, w);
    colsOut->clear(); for (int k = 0; k < nc; k++) colsOut->append(cols[k]);
    fillToolRows(t, 0, idx, n, cur, cols, w, nc, &tp_cs[2]);
}
QTableWidget *toolTable(QWidget *parent, const QRect &r) {
    auto *t = mk<QTableWidget>(parent, r);
    t->verticalHeader()->hide(); t->horizontalHeader()->setHighlightSections(false); t->horizontalHeader()->setFixedHeight(18);
    t->setSelectionBehavior(QAbstractItemView::SelectRows); t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers); t->setShowGrid(false); t->setTextElideMode(Qt::ElideNone);
    t->setStyleSheet(QString("QTableWidget { %1 }").arg(TextFit::dialogFontCss())); t->setWordWrap(false);
    t->horizontalHeader()->setStyleSheet(QString("QHeaderView::section { %1 padding: 1px 3px; }").arg(TextFit::dialogFontCss()));   /* the template font (the ribbon theme styles headers) */
    QPalette pal = t->palette(); pal.setColor(QPalette::Highlight, QColor(51, 153, 255)); pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Inactive, QPalette::Highlight, QColor(51, 153, 255)); pal.setColor(QPalette::Inactive, QPalette::HighlightedText, Qt::white); t->setPalette(pal);
    return t;
}
/* tw_edit_txt's rule (a commit on Enter / leaving the box): a plain number >= 0, else the old value stays */
static bool plainNum(const QString &s, double *out) {
    QByteArray b = s.toUtf8(); char *e; double d = strtod(b.constData(), &e); while (*e == ' ') e++;
    if (e == b.constData() || *e || !std::isfinite(d) || d < 0) return false;
    *out = d; return true;
}

/* ------------------------------------------------------------------------------------------------ OpDialog */
/* what the dialog shows (not the comment, which only its own box writes): a refresh when it changed */
static QByteArray opSig() {
    QByteArray b; b.append((const char *)&pending.p, sizeof pending.p); b.append((const char *)&pp_hf, sizeof pp_hf);
    int v[9] = { pp_page, tool_choice, tp_rapid, tp_ftc, tp_batch, F.ntools, tlib_ok ? tlib.n : -1, edit_camop, lk_pick };
    b.append((const char *)v, sizeof v); b.append((const char *)pp_tstate, sizeof pp_tstate); b.append(cam_msg);
    { unsigned char e[64 * 12]; int n = pf_dirty_sig(e, sizeof e); b.append((const char *)e, n); }   /* the template pages' record edits */
    b.append((const char *)&tp_factive, sizeof tp_factive); b.append((const char *)&tmf, sizeof tmf);   /* batch 12: the Tool page's filter */
    return b;
}
bool OpDialog::stale() const { return opSig() != sig_; }
OpDialog::OpDialog(QWidget *parent) : FloatDlg("", parent) {
    setClosable(false);                                 /* mcamui's window: NK_WINDOW_BORDER | TITLE | MOVABLE (no close box) */
    setObjectName("opDialog");
    setBodySize(du(0, 0, PP_W, PP_H).size());           /* mccoreres dialog 6544, 567 x 395 du */
    QWidget *b = body();
    /* the toolbar (core_ui PP_TB: InitToolBar + AddDefaultsButtons): 24 x 24 images in 34 x 33 buttons at the top; live ones by
       core_ui pp_tb_live (Tool Manager... = cui_hooks.tool_manager, phase 7; batch 12 Reload parameters from defaults file, Hide
       dialog), a click = pp_tb_click; the enabled state is set again in refresh() */
    int x = 0;
    for (int i = 0; i < PP_NTB; i++) {
        if (!PP_TB[i].bmp) { auto *sep = new QToolButton(b); sep->hide(); tb_ << sep; continue; }
        auto *t = mk<QToolButton>(b, QRect(0, 0, 34, 33)); t->setAutoRaise(true);
        t->setIcon(Icons::icon(QString("#file:mccoreres_BMPK_%1.png").arg(PP_TB[i].bmp), 24, 24)); t->setIconSize(QSize(24, 24));
        t->setToolTip(QString::fromUtf8(PP_TB[i].tip)); t->setEnabled(pp_tb_live(i) != 0);
        connect(t, &QToolButton::clicked, this, [this, i] { if (!pp_tb_live(i)) return; if (PP_TB[i].bmp == 10501) { preview(); return; } pp_tb_click(i); refresh(); emit changed(); });
        tb_ << t; (void)x;
    }
    /* Q19 (ours, user decision "make sure default can easily be reset"; core_ui cui_defaults.c): Mastercam has no such command -
       a separate button after its toolbar, labelled as this program's */
    dflBtn_ = mk<QToolButton>(b, QRect(0, 0, 150, 33)); dflBtn_->setText("Defaults file (ours)"); dflBtn_->setAutoRaise(true);
    dflBtn_->setToolTip("apprenticeCAM's own commands for the defaults file \"Save parameters to defaults file\" writes (not Mastercam's)");
    connect(dflBtn_, &QToolButton::clicked, this, [this] { defaultsMenu(); });
    tree_ = mk<QTreeWidget>(b, du(PP_FRAME[PPF_TREE].x, PP_FRAME[PPF_TREE].y, PP_FRAME[PPF_TREE].w, PP_FRAME[PPF_TREE].h));
    tree_->setHeaderHidden(true); tree_->setIndentation(16); tree_->setIconSize(QSize(16, 16)); tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(true); tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *it, int) {
        if (filling_) return;
        pp_tree_click(it->data(0, Qt::UserRole).toInt());   /* a page this program does not fill keeps the selection (ActiveNewPage) */
        refresh(); emit changed();
    });
    { const PpRect &r = PP_FRAME[PPF_QVLABEL]; auto *l = mk<QLabel>(b, du(r.x, r.y, r.w + 20, r.h + 2)); l->setText("Quick View Settings"); }   /* 9000 */
    { const PpRect &r = PP_FRAME[PPF_QV]; qv_ = mk<QTableWidget>(b, du(r.x, r.y, r.w, r.h));
      qv_->setColumnCount(2); qv_->horizontalHeader()->hide(); qv_->verticalHeader()->hide(); qv_->setShowGrid(false);
      qv_->setEditTriggers(QAbstractItemView::NoEditTriggers); qv_->setSelectionMode(QAbstractItemView::NoSelection); qv_->setFocusPolicy(Qt::NoFocus); qv_->setTextElideMode(Qt::ElideNone);   /* clipped, as mcamui's labels */
      qv_->setStyleSheet(QString("QTableWidget { %1 }").arg(TextFit::dialogFontCss())); qv_->setWordWrap(false);
      int lw = duX(r.w); qv_->setColumnWidth(0, (int)(lw * 0.45f)); qv_->setColumnWidth(1, (int)(lw * 0.53f - 8)); }   /* InitQuickViewList: 45 % / 53 % */
    /* legend: 3608 = 0x5e5 (edited), 3611 = 0x5da (disabled), UIBitmap transparent 0xc0c0c0 (OnInitDialog) */
    { const PpRect &a = PP_FRAME[PPF_LEG1], &c = PP_FRAME[PPF_LEG2];
      picLabel(b, du(a.x, a.y, a.w, a.h), "mccoreres_BMPK_1509.png"); picLabel(b, du(c.x, c.y, c.w, c.h), "mccoreres_BMPK_1498.png");
      const PpRect &t1 = PP_FRAME[PPF_LEG1T], &t2 = PP_FRAME[PPF_LEG2T];
      mk<QLabel>(b, du(t1.x, t1.y, t1.w, t1.h + 2))->setText("= edited"); mk<QLabel>(b, du(t2.x, t2.y, t2.w, t2.h + 2))->setText("= disabled"); }
    { const PpRect &r = PP_FRAME[PPF_PAGE]; page_ = mk<QWidget>(b, du(r.x, r.y, r.w, r.h)); }   /* the page frame 3667 */
    { const PpRect &r = PP_FRAME[PPF_STATUS];          /* ours, user decision UI-5: a longer message wraps and scrolls (the full text in its tooltip) */
      auto *sa = mk<QScrollArea>(b, du(r.x, r.y, r.w, r.h)); sa->setFrameShape(QFrame::NoFrame); sa->setWidgetResizable(true);
      sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); sa->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }");
      status_ = new QLabel; dlgFont(status_); status_->setWordWrap(true); status_->setAlignment(Qt::AlignLeft | Qt::AlignTop); sa->setWidget(status_); }
    /* OK / Cancel / Apply / Help (core_ui PP_BUTTONS): the uictrls bitmaps (40 x 20) with the captions as tooltips; no pack:
       the tooltip text; Apply (batch 12) = core_ui pp_apply, enabled by pp_apply_enabled in refresh(); Help not ported */
    for (int i = 0; i < 4; i++) {
        const PpButton &bt = PP_BUTTONS[i]; const PpRect &r = PP_FRAME[bt.rect];
        auto *p = mk<QPushButton>(b, du(r.x, r.y, r.w, r.h));
        QString img = QString("#file:uictrls_BMPK_%1.png").arg(bt.bmp);
        if (Icons::hasImage(img)) { p->setIcon(Icons::icon(img, 40, 20)); p->setIconSize(QSize(40, 20)); } else p->setText(QString::fromUtf8(bt.tip));
        p->setToolTip(QString::fromUtf8(bt.tip)); p->setEnabled(bt.live != 0);
        btn_[i] = p;
    }
    connect(btn_[0], &QPushButton::clicked, this, [this] { ok(); });   /* OK (core_ui pp_ok / gen_*) */
    connect(btn_[1], &QPushButton::clicked, this, [this] { pp_cancel(); emit done(); });
    connect(btn_[2], &QPushButton::clicked, this, [this] { if (!pp_apply_enabled()) return; pp_apply(); refresh(); emit changed(); });   /* OnApplyPb */
}

/* OK. An existing op whose dialog is unchanged is left as it was (core_ui pp_ok_unchanged: Mastercam's edit_operation
   memcmp_op rule). Otherwise the dialog is applied (gen_begin) and the toolpath computed on a worker thread (gen_run:
   cam_generate on a copy, reading the part only) while the windows stay painted. Meanwhile g_regen_busy is set (the
   main window refuses every command, open / new / close / exit, drop, play timer) and InputHold drops all user input
   except to the progress window, which shows after 400 ms (ours, regenprogress.h: Mastercam's own regeneration progress
   UI is NOT DECODED) and is application-modal. Cancel / Esc / its close box stop the Dynamic engine (mcce_abort_set) and
   leave the op as it was before OK with the dialog open; for the other engines (no abort points) and after a custom
   tool was registered (gen_can_cancel 0) Cancel is shown disabled with a tooltip. The result is committed on the GUI
   thread (gen_end) exactly as generate_pending did. */
bool g_regen_busy;
/* the computation of JOB on a worker thread with the progress window (OK's machinery above); RUN_END(ok, cancelled) on the GUI
   thread (in withGL) */
void OpDialog::runJob(CamOp &job, const std::function<void(int, int)> &runEnd, QWidget *parent) {
    busy_ = true; g_regen_busy = true; mcce_abort_set(0);
    int canc = gen_can_cancel() && gen_abortable(&job);
    InputHold hold; qApp->installEventFilter(&hold);
    RegenProgress prog(parent ? parent : this, canc, gen_can_cancel() ? "This operation type cannot be interrupted here (ours)"
                                                     : "A new tool was added to the part for this operation: it cannot be cancelled (ours)");
    hold.allow = &prog;
    bool cancelled = false;
    prog.onCancel = [&] { if (!cancelled) { cancelled = true; mcce_abort_set(1); prog.setLabelText("Cancelling..."); } };
    QTimer showT; showT.setSingleShot(true); connect(&showT, &QTimer::timeout, &prog, [&] { prog.show(); prog.raise(); prog.activateWindow(); }); showT.start(400);
    std::atomic<int> okv{ 0 };
    QThread *t = QThread::create([&] { okv = gen_run(&job); });
    t->setStackSize(64u << 20);
    QEventLoop loop;
    connect(t, &QThread::finished, &loop, &QEventLoop::quit);
    testInput(&prog);
    t->start();
    if (!t->isFinished()) loop.exec();
    t->wait(); delete t;
    showT.stop(); prog.hide(); qApp->removeEventFilter(&hold); mcce_abort_set(0);
    auto end = [&] { runEnd(okv, cancelled ? 1 : 0); };
    if (withGL) withGL(end); else end();
    busy_ = false; g_regen_busy = false;
}
void OpDialog::ok() {
    if (busy_ || g_regen_busy) return;
    {   /* edit_operation: no change -> nothing done; batch 11 post values; a change that marks the op dirty -> not computed
           (core_ui pp_ok_quick, cui_dirty.c); Q20: after a preview, compared with the previewed op (cui_preview.c) */
        int quick = 0; auto q = [&] { quick = pp_ok_quick(); };
        if (withGL) withGL(q); else q();
        if (quick) { emit done(); return; }
    }
    CamOp job; memset(&job, 0, sizeof job); int run = 0;
    auto begin = [&] { run = gen_begin(&job); };
    if (withGL) withGL(begin); else begin();
    if (!run) { emit done(); return; }                  /* a message (no tool), as generate_pending */
    runJob(job, [&job](int okv, int cancelled) { gen_end(&job, okv, cancelled); });
    emit done();
}
/* Q20 Preview toolpath 10501 (core_ui cui_preview.c: OnPreviewToolpathPb): the dialog's op computed on a copy on the worker thread
   as OK does; the dialog stays open, the graphics show the preview in place of the op's toolpath */
void OpDialog::preview() {
    if (busy_ || g_regen_busy || !pv_live()) return;
    CamOp job; memset(&job, 0, sizeof job); int run = 0;
    auto begin = [&] { run = pv_begin(&job); };
    if (withGL) withGL(begin); else begin();
    if (!run) { refresh(); emit changed(); return; }
    runJob(job, [&job](int okv, int cancelled) { pv_end(&job, okv, cancelled); });
    refresh(); emit changed();
}
/* Q19 (ours): the defaults file's working copy - where it is, Reset defaults file to original, Restore a dated backup; each asks
   first (core_ui dfl_q) */
void OpDialog::defaultsMenu() {
    QMenu m(this);
    char wp[1700]; dfl_work_path(wp, sizeof wp);
    int has = dfl_has_copy();
    QAction *info = m.addAction(has ? QString("Working copy: %1").arg(QString::fromUtf8(wp))
                                    : QString("No copy yet - Mastercam's defaults file is read (a save makes the copy)"));
    info->setEnabled(false);
    m.addSeparator();
    QAction *reset = m.addAction("Reset defaults file to original (ours)..."); reset->setEnabled(has);
    QMenu *rm = m.addMenu("Restore a dated backup (ours)");
    static char names[256][160]; int nb = dfl_backup_names(names, 256);
    for (int i = nb - 1; i >= 0; i--) { QAction *a = rm->addAction(QString::fromUtf8(names[i])); a->setData(i); }
    rm->setEnabled(nb > 0);
    QAction *got = m.exec(dflBtn_->mapToGlobal(QPoint(0, dflBtn_->height())));
    if (!got) return;
    int asked = got == reset ? dfl_reset_ask() : got->parent() == rm || rm->actions().contains(got) ? dfl_restore_ask(got->data().toInt()) : 0;
    if (asked && dfl_q.pending) {
        auto r = QMessageBox::question(this, "apprenticeCAM", QString::fromUtf8(dfl_q.text), QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
        dfl_answer(r == QMessageBox::Ok);
    }
    refresh(); emit changed();
}
/* tests: MCAMUI_OK_INPUT=KIND:MS (KIND click | esc | close | main) - a synthesized event to the progress window's
   QWindow (as the window system delivers it: through the application's event filters and QWidgetWindow) MS ms after OK;
   main = a click on the main window, which must be dropped */
void OpDialog::testInput(QWidget *prog) {
    QString spec = qEnvironmentVariable("MCAMUI_OK_INPUT"); if (spec.isEmpty()) return;
    QString kind = spec.section(':', 0, 0); int ms = spec.section(':', 1).toInt();
    QTimer::singleShot(ms, prog, [prog, kind, this] {
        QWindow *w = prog->windowHandle();
        if (kind == "main") { QWidget *mw = window()->parentWidget() ? window()->parentWidget()->window() : nullptr; w = mw ? mw->windowHandle() : nullptr; }
        if (!w) { fprintf(stderr, "MCAMUI_OK_INPUT: no window (progress shown %d)\n", prog->isVisible()); return; }
        if (kind == "click" || kind == "main") {
            QPoint p(10, 10);
            if (kind == "click") if (auto *b = prog->findChild<QPushButton *>()) p = b->mapTo(prog, b->rect().center());
            QMouseEvent pr(QEvent::MouseButtonPress, QPointF(p), QPointF(p), w->mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent rl(QEvent::MouseButtonRelease, QPointF(p), QPointF(p), w->mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(w, &pr); QCoreApplication::sendEvent(w, &rl);
        } else if (kind == "esc") {
            QKeyEvent so(QEvent::ShortcutOverride, Qt::Key_Escape, Qt::NoModifier), kp(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier), kr(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
            QCoreApplication::sendEvent(w, &so); if (!so.isAccepted()) QCoreApplication::sendEvent(w, &kp); QCoreApplication::sendEvent(w, &kr);
        } else if (kind == "close") w->close();
        fprintf(stderr, "MCAMUI_OK_INPUT: %s sent (progress shown %d)\n", kind.toUtf8().constData(), prog->isVisible());
    });
}

void OpDialog::refresh() {
    if (!show_params) return;
    filling_ = true;
    pp_frame_begin();
    char t[160]; pp_title(t, sizeof t); setTitle(QString::fromUtf8(t));
    {   int x = 0, hst = pp_hst(&pending.p);           /* separators 8 px; the feed speed calculator only for 0x84 / 0x86 / 0x87 */
        for (int i = 0; i < PP_NTB; i++) {
            if (PP_TB[i].hst && !hst) { tb_[i]->hide(); continue; }
            if (PP_TB[i].bmp == 10501 && !pv_shown()) { tb_[i]->hide(); continue; }   /* Q20: AddDefaultsButtons' opcodes only */
            if (!PP_TB[i].bmp) { x += 8; continue; }
            tb_[i]->move(x, 0); tb_[i]->show(); x += 34;
            tb_[i]->setEnabled(pp_tb_live(i) != 0);
        }
        if (dflBtn_) { dflBtn_->move(x + 16, 0); dflBtn_->show(); } }
    btn_[2]->setEnabled(pp_apply_enabled() != 0);         /* Apply: OnPageDataChanged / OnApplyPb (core_ui) */
    /* the tree (core_ui pp_tree_rows: FUN_18014a700's order, image 0 clean / 1 dirty / 2 disabled) */
    {
    PpTreeRow tr[64]; int nt = pp_tree_rows(tr, 64);
    /* perf: the same rows as the tree shows: only the selection is set again (as the rebuild selects pp_page's row) */
    QByteArray tsig; for (int i = 0; i < nt; i++) { tsig.append((const char *)&tr[i].depth, sizeof tr[i].depth); tsig.append((const char *)&tr[i].state, sizeof tr[i].state);
        tsig.append((const char *)&tr[i].slot, sizeof tr[i].slot); tsig.append((const char *)&tr[i].done, sizeof tr[i].done); tsig.append(tr[i].title); tsig.append('\0'); }
    if (tsig == treeSig_ && tree_->topLevelItemCount()) {
        for (QTreeWidgetItem *it : tree_->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive)) { bool on = it->data(0, Qt::UserRole).toInt() == pp_page; if (it->isSelected() != on) it->setSelected(on); }
    } else {
    treeSig_ = tsig;
    tree_->clear();
    QVector<QTreeWidgetItem *> parents;
    static const int TI[3] = { 1477, 1509, 1498 };
    for (int i = 0; i < nt; i++) {
        while (parents.size() > tr[i].depth) parents.removeLast();
        QTreeWidgetItem *it = parents.isEmpty() ? new QTreeWidgetItem(tree_) : new QTreeWidgetItem(parents.last());
        it->setText(0, QString::fromUtf8(tr[i].title));
        { QIcon ic = Icons::icon(QString("#file:mccoreres_BMPK_%1.png").arg(TI[tr[i].state]), 16, 16);
          ic.addPixmap(ic.pixmap(16, 16), QIcon::Selected); it->setIcon(0, ic); }   /* the selected row shows the image as is (the style would tint it) */
        it->setData(0, Qt::UserRole, tr[i].slot);
        if (!tr[i].done) { it->setForeground(0, greyOn(tree_, QPalette::Base)); it->setFlags(Qt::ItemIsEnabled); }   /* not ported: the selection stays */
        if (tr[i].slot == pp_page) it->setSelected(true);
        parents.append(it);
    }
    tree_->expandAll(); } }
    /* Quick View Settings (core_ui pp_quickview_rows) */
    { const char *nm[10]; char v[10][64]; int nq = pp_quickview_rows(nm, v);
    qv_->setRowCount(nq);
    for (int i = 0; i < nq; i++) for (int c = 0; c < 2; c++) {   /* perf: a cell only when its text changed */
        QString t = QString::fromUtf8(c ? v[i] : nm[i]); QTableWidgetItem *it = qv_->item(i, c);
        if (!it || it->text() != t) qv_->setItem(i, c, new QTableWidgetItem(t));
        if (c) qv_->setRowHeight(i, 16);
    }
    TextFit::fitColumns(qv_);                         /* ours, user decision UI-5: the columns fit their names / values */
    } status_->setText(QString::fromUtf8(cam_msg)); status_->setToolTip(status_->text());
    buildPage();
    filling_ = false;
    sig_ = opSig();
}

/* ---- the pages */
void OpDialog::newRow(int h, int cols) {
    rowBox_ = new QWidget; rowBox_->setFixedHeight(h);
    row_ = new QGridLayout(rowBox_); row_->setContentsMargins(0, 0, 0, 0); row_->setHorizontalSpacing(2);
    for (int c = 0; c < cols; c++) row_->setColumnStretch(c, 1);
    rows_->addWidget(rowBox_);
    rowH_ = h; rowCols_ = cols; cell_ = 0;
}
void OpDialog::cell(QWidget *w) {
    if (!row_ || cell_ >= rowCols_) newRow(rowH_, rowCols_);   /* nuklear: a dynamic row repeats for the next widgets */
    dlgFont(w); row_->addWidget(w, 0, cell_++);
}
static OpDialog *U(void *u) { return static_cast<OpDialog *>(u); }
/* PpOps over Qt widgets; mode 1 = a change: control pendK_ returns pendV_, the others their values, no widgets */
static void qp_row(void *u, float h, int d, int cols) { OpDialog *p = U(u); if (p->mode_) return; p->newRow(d ? duY(h) : (int)h, cols); }
static void qp_row_pic(void *u, float h, float pad, float w, const char *file) {
    OpDialog *p = U(u); if (p->mode_) return;
    p->newRow(duY(h), 1); auto *box = new QWidget; auto *hl = new QHBoxLayout(box); hl->setContentsMargins((int)pad, 0, 0, 0);
    auto *pic = new QWidget; pic->setFixedSize(duX(w), duY(h)); picLabel(pic, QRect(0, 0, duX(w), duY(h)), file); hl->addWidget(pic); hl->addStretch(1);
    p->cell(box); p->row_ = nullptr;
}
static void qp_row_static_pic(void *u, float h, float w, const char *file) {
    OpDialog *p = U(u); if (p->mode_) return;
    p->newRow(duY(h), 1); auto *box = new QWidget; auto *hl = new QHBoxLayout(box); hl->setContentsMargins(0, 0, 0, 0);
    auto *pic = new QWidget; pic->setFixedSize(duX(w), duY(h)); picLabel(pic, QRect(0, 0, duX(w), duY(h)), file); hl->addWidget(pic); hl->addStretch(1);
    p->cell(box); p->row_ = nullptr;
}
static void qp_pic(void *u, float w, float h, const char *file) {
    OpDialog *p = U(u); if (p->mode_) return;
    auto *box = new QWidget; picLabel(box, QRect(0, 0, duX(w), duY(h)), file); p->cell(box);
}
static void qp_spacing(void *u) { OpDialog *p = U(u); if (p->mode_) return; p->cell(new QWidget); }
static QString shortLabel(const char *l) { return QString::fromUtf8(l[0] == '#' ? l + 1 : l); }
static double qp_prop(void *u, const char *label, double v, double lo, double hi, double step) {
    OpDialog *p = U(u); int k = p->k_++; (void)step;
    if (p->mode_) return k == p->pendK_ ? (p->pendV_ < lo ? lo : p->pendV_ > hi ? hi : p->pendV_) : v;   /* clamped as nk_property */
    auto *w = new QWidget; auto *hl = new QHBoxLayout(w); hl->setContentsMargins(0, 0, 0, 0); hl->setSpacing(4);
    auto *l = new QLabel(shortLabel(label)); dlgFont(l); hl->addWidget(l, 1);
    auto *le = new QLineEdit(QString::asprintf("%.2f", v)); dlgFont(le); le->setFixedWidth(90); le->setAlignment(Qt::AlignRight); hl->addWidget(le);   /* nk_property: 2 decimals shown */
    QObject::connect(le, &QLineEdit::editingFinished, p, [p, k, le] { if (!le->isModified()) return; bool ok; double d = le->text().toDouble(&ok); if (ok) p->edited(k, d); });
    p->cell(w); p->ctls_.append({ 0, shortLabel(label), le, lo, hi });
    return v;
}
static int qp_iprop(void *u, const char *label, int v, int lo, int hi) {
    OpDialog *p = U(u); int k = p->k_++;
    if (p->mode_) { if (k != p->pendK_) return v; int n = (int)p->pendV_; return n < lo ? lo : n > hi ? hi : n; }
    auto *w = new QWidget; auto *hl = new QHBoxLayout(w); hl->setContentsMargins(0, 0, 0, 0); hl->setSpacing(4);
    auto *l = new QLabel(shortLabel(label)); dlgFont(l); hl->addWidget(l, 1);
    auto *le = new QLineEdit(QString::number(v)); dlgFont(le); le->setFixedWidth(90); le->setAlignment(Qt::AlignRight); hl->addWidget(le);
    QObject::connect(le, &QLineEdit::editingFinished, p, [p, k, le] { if (!le->isModified()) return; bool ok; int d = le->text().toInt(&ok); if (ok) p->edited(k, d); });
    p->cell(w); p->ctls_.append({ 1, shortLabel(label), le, (double)lo, (double)hi });
    return v;
}
static int qp_check(void *u, const char *label, int v) {
    OpDialog *p = U(u); int k = p->k_++;
    if (p->mode_) return k == p->pendK_ ? p->pendV_ != 0 : v;
    auto *c = new QCheckBox(QString::fromUtf8(label)); c->setChecked(v != 0);
    QObject::connect(c, &QCheckBox::toggled, p, [p, k](bool on) { p->edited(k, on); });
    p->cell(c); p->ctls_.append({ 2, QString::fromUtf8(label), c, 0, 1 });
    return v;
}
static int qp_combo(void *u, const char *const *items, int n, int cur, float pw, float ph) {
    OpDialog *p = U(u); int k = p->k_++; (void)pw; (void)ph;
    if (p->mode_) { if (k != p->pendK_) return cur; int i = (int)p->pendV_; return i >= 0 && i < n ? i : cur; }
    auto *c = new QComboBox; for (int i = 0; i < n; i++) c->addItem(QString::fromUtf8(items[i])); c->setCurrentIndex(cur);
    QObject::connect(c, QOverload<int>::of(&QComboBox::activated), p, [p, k](int i) { p->edited(k, i); });
    int ord = 0; for (const auto &x : p->ctls_) if (x.kind == 3) ord++;
    p->cell(c); p->ctls_.append({ 3, QString("combo#%1").arg(ord), c, 0, (double)n });
    return cur;
}
static void qp_label(void *u, const char *t, int style) {
    OpDialog *p = U(u); if (p->mode_) return;
    auto *l = new QLabel(QString::fromUtf8(t)); if (style) l->setStyleSheet(textCss(p, QColor(180, 30, 30)));   /* mcamui's error colour */
    p->cell(l); p->ctls_.append({ 4, QString::fromUtf8(t), l, 0, 0 });
}
static void qp_option(void *u, const char *t, int on) {
    OpDialog *p = U(u); if (p->mode_) return;
    auto *r = new QRadioButton(QString::fromUtf8(t)); r->setAutoExclusive(false); r->setChecked(on != 0); r->setEnabled(false);   /* not ported */
    p->cell(r); p->ctls_.append({ 5, QString::fromUtf8(t), r, 0, 0 });
}
/* a push button / a live radio (the Planes page, core_ui cui_planes.c): 1 = clicked */
static int qp_button(void *u, const char *t, int en) {
    OpDialog *p = U(u); int k = p->k_++;
    if (p->mode_) return k == p->pendK_;
    auto *b = new QPushButton(QString::fromUtf8(t)); b->setEnabled(en != 0);   /* greyed: not ported */
    QObject::connect(b, &QPushButton::clicked, p, [p, k] { p->edited(k, 1); });
    p->cell(b); p->ctls_.append({ 6, QString::fromUtf8(t), b, 0, 0 });
    return 0;
}
static int qp_radio(void *u, const char *t, int on) {
    OpDialog *p = U(u); int k = p->k_++;
    if (p->mode_) return k == p->pendK_ && !on;
    auto *r = new QRadioButton(QString::fromUtf8(t)); r->setAutoExclusive(false); r->setChecked(on != 0);
    QObject::connect(r, &QRadioButton::clicked, p, [p, k, on] { if (!on) p->edited(k, 1); else p->refresh(); });   /* nk_option_label: a click on the selected one changes nothing */
    p->cell(r); p->ctls_.append({ 5, QString::fromUtf8(t), r, 0, 0 });
    return 0;
}
static const PpOps QT_PP_OPS = { qp_row, qp_row_pic, qp_row_static_pic, qp_pic, qp_spacing, qp_prop, qp_iprop, qp_check, qp_combo, qp_label, qp_option, qp_button, qp_radio };

void OpDialog::edited(int k, double v) {
    if (filling_) return;
    CamParams before = pending.p; OpHFlags hb = pp_hf;
    mode_ = 1; k_ = 0; pendK_ = k; pendV_ = v;
    pp_page_body(pp_page_shown(), &QT_PP_OPS, this);
    mode_ = 0; pendK_ = -1;
    pp_note_change(&before, &hb);                       /* SetPageDataChanged -> the tree item dirty */
    refresh(); emit changed();
}

void OpDialog::buildPage() {
    {   /* perf: the same template page with the same controls: the values in place (tmplUpdate), no rebuild */
        int pg0 = pp_page_shown();
        if (tmplBox_ && tmplPg_ == pg0 && pf_active(pg0)) {
            PfShown c[160]; int n = pf_ctls(c, 160);
            if (tmplSame(c, n)) { tmplUpdate(c, n); return; }
        }
    }
    tmpl_.clear(); tmplShown_.clear(); tmplOn_.clear(); tmplPg_ = -1; tmplBox_ = nullptr;
    for (QObject *c : page_->children()) if (c->isWidgetType()) retireWidget(static_cast<QWidget *>(c));   /* retire.h */
    ctls_.clear(); rows_ = nullptr; row_ = nullptr; rowBox_ = nullptr;
    int pg = pp_page_shown(), px, py, pw, ph; char pf[64];
    QWidget *box = new QWidget(page_);
    if (!pf_active(pg) && pp_page_pic(&pending.p, pg, &px, &py, &pw, &ph, pf, sizeof pf)) {
        picLabel(page_, du(px, py, pw, ph), QString::fromUtf8(pf));
        PpRect g = pp_wrap_rect(px, py, pw, ph); box->setGeometry(du(g.x, g.y, g.w, g.h));
    } else box->setGeometry(0, 0, page_->width(), page_->height());
    box->show();
    if (pf_active(pg)) { buildTemplate(box); return; }   /* its pictures are its statics */
    if (pg == PG_TOOL) { buildTool(box); return; }
    if (pg == PG_LINK) { buildLink(box); return; }
    rows_ = new QVBoxLayout(box); rows_->setContentsMargins(4, 2, 4, 2); rows_->setSpacing(2);   /* nuklear window padding 4 x 2, spacing 2 */
    mode_ = 0; k_ = 0;
    pp_page_body(pg, &QT_PP_OPS, this);
    rows_->addStretch(1);
}

/* the Linking page (core_ui lk_frame / lk_rows / lk_set): [check] Button... value, Absolute / Incremental per row */
void OpDialog::buildLink(QWidget *box) {
    lk_frame();                                         /* the page's own writes, as mcamui page_linking each frame */
    auto *v = new QVBoxLayout(box); v->setContentsMargins(4, 2, 4, 2); v->setSpacing(2);
    int drill = pending.p.type == CAM_DRILL;
    auto change = [this](const std::function<void()> &fn) {
        if (filling_) return;
        CamParams before = pending.p; OpHFlags hb = pp_hf; fn(); pp_note_change(&before, &hb); refresh(); emit changed();
    };
    auto spacer = [v](int h) { auto *s = new QWidget; s->setFixedHeight(h); v->addWidget(s); };
    LkRow r[5]; int n = lk_rows(r);
    for (int i = 0; i < n; i++) {
        const LkRow row = r[i]; int id = row.id;
        if (i > 0) spacer(8);
        auto *line = new QWidget; line->setFixedHeight(22); auto *hl = new QHBoxLayout(line); hl->setContentsMargins(0, 0, 0, 0); hl->setSpacing(2);
        if (row.has_on) { auto *c = new QCheckBox; c->setFixedWidth(22); c->setChecked(row.on != 0); hl->addWidget(c);
            connect(c, &QCheckBox::toggled, this, [change, id](bool on) { change([id, on] { lk_set(id, LK_ON, on); }); });
            ctls_.append({ 2, QString("lk:%1,0").arg(id), c, 0, 1 }); }
        else { auto *s = new QWidget; s->setFixedWidth(22); hl->addWidget(s); }
        auto *b = new QPushButton(QString::fromUtf8(row.btn)); dlgFont(b); b->setFixedWidth(100); b->setEnabled(row.live != 0); hl->addWidget(b);
        connect(b, &QPushButton::clicked, this, [change, id] { change([id] { lk_set(id, LK_PICK, 0); }); });   /* arms lk_pick, prompt 5321 */
        ctls_.append({ 6, QString("lk:%1,3").arg(id), b, 0, 0 });
        if (row.live) { auto *le = new QLineEdit(QString::asprintf("%.2f", row.shown)); dlgFont(le); le->setFixedWidth(120); hl->addWidget(le);
            connect(le, &QLineEdit::editingFinished, this, [change, id, le] { if (!le->isModified()) return; bool ok; double d = le->text().toDouble(&ok);
                if (ok && d >= -1e6 && d <= 1e6) change([id, d] { lk_set(id, LK_VAL, d); }); });
            ctls_.append({ 0, QString("lk:%1,1").arg(id), le, -1e6, 1e6 }); }
        else { auto *l = new QLabel(QString::asprintf("%.4f", row.shown)); dlgFont(l); l->setFixedWidth(120); l->setStyleSheet(cssColor(greyOn(this))); hl->addWidget(l);
            ctls_.append({ 4, QString("lk:%1,1").arg(id), l, 0, 0 }); }
        hl->addStretch(1); v->addWidget(line);
        auto *rl = new QWidget; rl->setFixedHeight(20); auto *h2 = new QHBoxLayout(rl); h2->setContentsMargins(22 + 2, 0, 0, 0); h2->setSpacing(2);
        auto *ab = new QRadioButton("Absolute"), *ib = new QRadioButton("Incremental"); dlgFont(ab); dlgFont(ib);
        ab->setAutoExclusive(false); ib->setAutoExclusive(false); ab->setChecked(!row.inc); ib->setChecked(row.inc != 0);
        ab->setFixedWidth(100); ib->setFixedWidth(110); ab->setEnabled(row.live != 0); ib->setEnabled(row.live != 0);
        connect(ab, &QRadioButton::clicked, this, [change, id] { change([id] { lk_set(id, LK_INC, 0); }); });
        connect(ib, &QRadioButton::clicked, this, [change, id] { change([id] { lk_set(id, LK_INC, 1); }); });
        ctls_.append({ 2, QString("lk:%1,2").arg(id), ib, 0, 1 });
        h2->addWidget(ab); h2->addWidget(ib); h2->addStretch(1); v->addWidget(rl);
        if (id == LK_CLR) {                              /* "Use clearance only at the start and end of operation" (1059), live with Clearance on */
            auto *c = new QCheckBox("Use clearance only at the start and end of operation"); dlgFont(c); c->setChecked(pp_hf.clr_end != 0); c->setEnabled(pp_hf.clr_on != 0);
            c->setFixedHeight(20); v->addWidget(c);
            connect(c, &QCheckBox::toggled, this, [change](bool on) { change([on] { lk_set_clr_end(on); }); });
            ctls_.append({ 2, "lk:0,0", c, 0, 1 });
        }
    }
    if (drill) { auto *c = new QCheckBox("Subprogram"); dlgFont(c); c->setEnabled(false); c->setFixedHeight(20); v->addWidget(c); }   /* 25593: 1434 not ported */
    v->addStretch(1);
}

/* the Tool page (core_ui tp_ctls: dialog 25524 / 6556 positions in draw order) */
void OpDialog::buildTool(QWidget *box) {
    if (otp_wait_new >= 0 && tedit_index < -1) tp_new_tool_done();   /* AddNewTool: the wizard closed (mcamui page_tool) */
    auto change = [this](const std::function<void()> &fn) {
        if (filling_) return;
        CamParams before = pending.p; OpHFlags hb = pp_hf; fn(); pp_note_change(&before, &hb); refresh(); emit changed();
    };
    TpCtl c[64]; int n = tp_ctls(c, 64);
    for (int i = 0; i < n; i++) {
        const TpCtl k = c[i]; QRect r = du(k.x, k.y, k.w, k.h); char v[160] = "";
        switch (k.kind) {
        case TPK_LIST: {
            /* batch 12: the rows = core_ui tp_rows (the part's tools, Filter Active 25263 applied); a row -> its part tool */
            auto *t = toolTable(box, r);
            { int idx[4096], nr = tp_rows(idx, 4096); fillToolRows(t, idx, nr, tool_choice, &tpCols_); }
            connect(t->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int s) {   /* LVN_COLUMNCLICK: OnListClickColumn -> SortList (columns >= 2 shown) */
                if (filling_ || s < 0 || s >= tpCols_.size()) return;
                tp_sortlist(tpCols_[s]); refresh();
            });
            auto rowTool = [](int row) { int idx[4096], nr = tp_rows(idx, 4096); return row >= 0 && row < nr ? idx[row] : -1; };
            connect(t, &QTableWidget::cellClicked, this, [change, rowTool](int row, int) { int ti = rowTool(row); if (ti >= 0) change([ti] { tp_pick(ti); }); });   /* OnItemSelect */
            t->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(t, &QTableWidget::customContextMenuRequested, this, [this, t, rowTool](const QPoint &pt) {   /* the row under the click first, then menu 1247 */
                int row = rowTool(t->rowAt(pt.y())); if (row >= 0) tp_pick(row);
                toolMenu(t->viewport()->mapToGlobal(pt));
            });
            ctls_.append({ 7, QString("tp:%1").arg(k.id), t, 0, 0 });
        } break;
        case TPK_LABEL: { auto *l = mk<QLabel>(box, r.adjusted(0, -1, 0, 3)); l->setText(QString::fromUtf8(k.text)); l->setEnabled(k.ref == 0); } break;
        case TPK_RO: { auto *e = mk<QLineEdit>(box, r); tp_ctl_text(k.id, v, sizeof v); e->setText(QString::fromUtf8(v)); e->setReadOnly(true);
            ctls_.append({ 4, QString("tp:%1").arg(k.id), e, 0, 0 }); } break;
        case TPK_EDIT: { auto *e = mk<QLineEdit>(box, r); tp_ctl_text(k.id, v, sizeof v); e->setText(QString::fromUtf8(v)); int id = k.id;
            connect(e, &QLineEdit::editingFinished, this, [change, e, id] { if (!e->isModified()) return; double d; if (plainNum(e->text(), &d)) change([id, d] { tp_ctl_set(id, d); }); else e->setModified(false); });
            ctls_.append({ 0, QString("tp:%1").arg(k.id), e, 0, 1e30 }); } break;
        case TPK_DIV: { auto *f = mk<QFrame>(box, QRect(r.x(), r.y() + duY(4), r.width(), 1)); f->setStyleSheet("background: rgb(200,200,200);"); } break;   /* UIDivider */
        case TPK_BTN: { auto *b = mk<QPushButton>(box, r); b->setText(QString::fromUtf8(k.text)); b->setEnabled(k.ref == 0); int id = k.id;
            if (k.ref == 0) connect(b, &QPushButton::clicked, this, [change, id] { change([id] { tp_ctl_set(id, 1); }); });
            ctls_.append({ 6, QString("tp:%1").arg(k.id), b, 0, 0 }); } break;
        case TPK_CHECK: { auto *cb = mk<QCheckBox>(box, r.adjusted(0, -2, 0, 4)); cb->setText(QString::fromUtf8(k.text)); cb->setChecked(tp_ctl_check(k.id) != 0); cb->setEnabled(k.ref == 0); int id = k.id;
            if (k.ref == 0) connect(cb, &QCheckBox::toggled, this, [change, id](bool on) { change([id, on] { tp_ctl_set(id, on); }); });
            ctls_.append({ 2, QString("tp:%1").arg(k.id), cb, 0, 1 }); } break;
        case TPK_COMBO: { auto *cb = mk<QComboBox>(box, r.adjusted(0, -2, 0, 4)); cb->addItem(TP_DIR[0]); cb->addItem(TP_DIR[1]); cb->setCurrentIndex(tp_ctl_combo()); int id = k.id;
            connect(cb, QOverload<int>::of(&QComboBox::activated), this, [change, id](int i) { change([id, i] { tp_ctl_set(id, i); }); });
            ctls_.append({ 3, QString("tp:%1").arg(k.id), cb, 0, 2 }); } break;
        case TPK_TEXTBOX: { auto *e = mk<QPlainTextEdit>(box, r); e->setPlainText(QString::fromUtf8(pending.name));   /* the op comment (op +6, 0x77 chars; our buffer 63) */
            connect(e, &QPlainTextEdit::textChanged, this, [e] { tp_set_comment(e->toPlainText().toUtf8().constData()); });
            ctls_.append({ 8, QString("tp:%1").arg(k.id), e, 0, 0 }); } break;
        case TPK_GROUP: { auto *g = mk<QGroupBox>(box, r); g->setTitle(QString::fromUtf8(k.text)); g->setEnabled(false); g->lower(); } break;
        }
    }
}

/* user decision UI-3: a NOT DECODED control (pf_ctls nd 1: the core appends " - NOT DECODED" to its text) shows Mastercam's text
   only; the marker is the app's marker colour and a "NOT DECODED" tooltip (textfit.cpp markOurs). The core's text: property ndText */
static QString ndStrip(QString t) {
    if (t.endsWith(" - NOT DECODED")) t.chop(14); else if (t == "NOT DECODED") t.clear();
    return t;
}
void OpDialog::ndMark(QWidget *w, const PfShown &k) {
    QString full = QString::fromUtf8(k.text), t = ndStrip(full);
    w->setProperty("coreText", full);
    if (auto *l = qobject_cast<QLabel *>(w)) l->setText(t);
    else if (auto *g = qobject_cast<QGroupBox *>(w)) g->setTitle(t);
    else if (auto *b = qobject_cast<QAbstractButton *>(w)) b->setText(t);
    else if (auto *e = qobject_cast<QLineEdit *>(w)) e->setText(t);
    else if (auto *cb = qobject_cast<QComboBox *>(w)) { for (int q = 0; q < cb->count(); q++) if (cb->itemText(q) == full) cb->setItemText(q, t); }
    TextFit::markOurs(w, k.tip[0] ? QString("NOT DECODED: %1").arg(QString::fromUtf8(k.tip)) : QString("NOT DECODED"));
}
/* UI-6 nudges (px): kind, id, template w / h (du), a text prefix ("" = any), dx, dy, dw */
struct PfNudge { int kind, id, w, h; const char *text; int dx, dy, dw; };
static const PfNudge PF_NUDGE[] = {
    { PFK_COMBO, 3202, 179, 30, "", 0, 0, 16 },                          /* 3205 Dynamic Contour "Motion > Gap size, retract": the combo +16 for its */
    { PFK_GROUP, -1, 185, 28, "Motion > Gap size, retract", 0, 0, 16 },  /*   "When avoiding a boundary AND exceeding a distance" item, its frame +16 */
    { PFK_STATIC, 1757, 165, 8, "Tolerance for remachining", -14, 0, 0 },/* 3003 Pocket roughing: the label 14 px left (its left neighbour ends ~50 px before) */
    { PFK_PUSH, 3209, 36, 18, "De-select", 0, 0, 4 },                    /* 3219 HST entry: "De-select..." +4 (inside its frame) */
    { PFK_STATIC, 1426, 48, 10, "around corners", 0, 2, 0 },             /* 2940 Contour: the label's second line 2 px down (it touched "Roll cutter") */
};
/* a template page (core_ui cui_opdlg.c pf_ctls): every control at its template position, greyed when not live (mcamui
   opparams_ui.inc page_template); a change goes to pf_set. Test keys: "pf:ID" */
void OpDialog::buildTemplate(QWidget *box) {
    auto change = [this](const std::function<void()> &fn) {
        if (filling_) return;
        CamParams before = pending.p; OpHFlags hb = pp_hf; fn(); pp_note_change(&before, &hb); refresh(); emit changed();
    };
    PfShown c[160]; int n = pf_ctls(c, 160);
    QVector<QGroupBox *> groups; QVector<int> oursMarks;
    const int pageDlg = pf_page_dialog(pp_page_shown());   /* the page's template dialog (its bitmap buttons, UI-5) */
    tmplPg_ = pp_page_shown(); tmplBox_ = box; tmpl_.resize(n); tmplShown_.resize(n); tmplOn_.resize(n);
    for (int i = 0; i < n; i++) {
        const PfShown k = c[i]; QRect r = du(k.x, k.y, k.w, k.h); int id = k.id; QString key = QString("pf:%1").arg(id);
        QWidget *made = nullptr; QWidget *const before = box->children().isEmpty() ? nullptr : qobject_cast<QWidget *>(box->children().last());
        tmplShown_[i] = QString::fromUtf8(k.text); tmplOn_[i] = k.on;
        switch (k.kind) {
        case PFK_STATIC: { auto *l = mk<QLabel>(box, r.adjusted(0, -1, 0, 3)); l->setText(QString::fromUtf8(k.text));
            if (k.nd == 2) l->setStyleSheet(textCss(box, QColor(180, 30, 30)));   /* nd 2: this program's error text */
            if (k.tip[0]) { l->setStyleSheet(textCss(box, QColor(150, 90, 0))); l->setToolTip(QString::fromUtf8(k.tip)); } } break;   /* "(ours)" marker */
        case PFK_PIC: picLabel(box, r, QString::fromUtf8(k.pic)); break;
        case PFK_GROUP: if (pageDlg == 999 && id == 915) {   /* owner-drawn button: CMsRPocPg OnDrawItem FUN_1800dbc40 l.143243-310 draws
                                                                    mcmsurfres bitmap 165 (UI-5) */
            QLabel *pl = picLabel(box, r, "mcmsurfres_BMP_165.png"); pl->setProperty("coreText", QString::fromUtf8(k.text)); break; }
            [[fallthrough]];
        case PFK_CGROUP: { auto *g = mk<QGroupBox>(box, r); g->setTitle(QString::fromUtf8(k.text));
            if (k.kind == PFK_CGROUP) { g->setCheckable(true); g->setChecked(k.on != 0); g->setEnabled(k.live != 0);
                if (k.live) connect(g, &QGroupBox::toggled, this, [change, id](bool on) { change([id, on] { pf_set(id, on); }); });
                ctls_.append({ 2, key, g, 0, 1 }); }
            groups << g; } break;
        case PFK_EDIT: { auto *e = mk<QLineEdit>(box, r); e->setText(QString::fromUtf8(k.text)); e->setEnabled(k.live != 0);
            /* commits only a text that differs from the shown one (mcamui pf_edit_txt); the shown text is tmplShown_[i] (an
               in-place update replaces it) */
            if (k.live) connect(e, &QLineEdit::editingFinished, this, [this, change, e, id, i] { if (!e->isModified() || (i < tmplShown_.size() && e->text() == tmplShown_[i])) return; bool ok; double d = e->text().toDouble(&ok);
                if (ok && std::isfinite(d)) change([id, d] { pf_set(id, d); }); else e->setModified(false); });
            ctls_.append({ 0, key, e, -1e30, 1e30 }); } break;
        case PFK_COMBO: { auto *cb = mk<QComboBox>(box, du(k.x, k.y, k.w, 14).adjusted(0, -2, 0, 4));
            for (int q = 0; q < k.nitems; q++) cb->addItem(QString::fromUtf8(k.items[q]));
            if (k.cur >= 0 && k.cur < k.nitems) cb->setCurrentIndex(k.cur); else { cb->addItem(QString::fromUtf8(k.text)); cb->setCurrentIndex(cb->count() - 1); }
            cb->setEnabled(k.live != 0 && k.nitems > 0);
            if (k.live) connect(cb, QOverload<int>::of(&QComboBox::activated), this, [change, id](int q) { change([id, q] { pf_set(id, q); }); });
            ctls_.append({ 3, key, cb, 0, (double)k.nitems }); } break;
        case PFK_LIST: { auto *l = mk<QListWidget>(box, r);
            for (int q = 0; q < k.nitems; q++) l->addItem(QString::fromUtf8(k.items[q]));
            if (k.cur >= 0) l->setCurrentRow(k.cur);
            l->setEnabled(k.live != 0);
            if (k.live) connect(l, &QListWidget::itemClicked, this, [change, id, l](QListWidgetItem *it) { int q = l->row(it); change([id, q] { pf_set(id, q); }); });
            ctls_.append({ 3, key, l, 0, (double)k.nitems }); } break;
        case PFK_CHECK: if (k.h >= 40) {                  /* a check box the size of a frame: the template's control is a group box (BS_GROUPBOX,
                                                              e.g. mcmsurfres 1324 / 1326 / 1327 25412 "Corner rounding") checked through its title, as
                                                              PFK_CGROUP (textfit 2026-11-17: drawn as a check box, its text sat over the frame's controls) */
            auto *g = mk<QGroupBox>(box, r); g->setTitle(QString::fromUtf8(k.text)); g->setCheckable(true); g->setChecked(k.on != 0); g->setEnabled(k.live != 0);
            if (k.live) connect(g, &QGroupBox::toggled, this, [change, id](bool on) { change([id, on] { pf_set(id, on); }); });
            ctls_.append({ 2, key, g, 0, 1 }); groups << g; break; } else {
            auto *cb = mk<QCheckBox>(box, r.adjusted(0, -2, 0, 4)); cb->setText(QString::fromUtf8(k.text)); cb->setChecked(k.on != 0); cb->setEnabled(k.live != 0);
            if (k.h >= 20) cb->setProperty("textfit_multiline", true);   /* two lines high: BS_MULTILINE (mccoreres 6555 2252), wrapped (textfit.cpp) */
            if (k.live) connect(cb, &QCheckBox::toggled, this, [change, id](bool on) { change([id, on] { pf_set(id, on); }); });
            ctls_.append({ 2, key, cb, 0, 1 }); } break;
        case PFK_RADIO: { auto *rb = mk<QRadioButton>(box, r.adjusted(0, -2, 0, 4)); rb->setText(QString::fromUtf8(k.text)); rb->setAutoExclusive(false);
            rb->setChecked(k.on != 0); rb->setEnabled(k.live != 0);
            if (k.live) connect(rb, &QRadioButton::clicked, this, [this, change, id, i] { if (!(i < tmplOn_.size() && tmplOn_[i])) change([id] { pf_set(id, 1); }); else refresh(); });   /* a click on the selected one changes nothing (the state shown: tmplOn_[i]) */
            ctls_.append({ 5, key, rb, 0, 0 }); } break;
        case PFK_PUSH: { auto *b = mk<QPushButton>(box, r); b->setText(QString::fromUtf8(k.text)); b->setEnabled(k.live != 0);   /* live: a ported handler (core_ui pf_set) */
            /* Mastercam's bitmap buttons (UI-5): 999 / 718 owner-drawn, mcmsurfres 157 with Tip comp 692 at index 0, 153 at 1 (FUN_1800dbc40,
               init l.143596-602, FUN_1800dbdd0 l.144430-446); 1338 / 1339 / 690 UIBitmapButton mccoreres 5537, colour 0xC0C0C0 clear
               (FUN_1800865c0 l.87670-687). The caption stays the core's (coreText, the dump) */
            { QPixmap pm; if (pageDlg == 999 && id == 718) { int tc = 0; for (int q = 0; q < n; q++) if (c[q].id == 692 && c[q].kind == PFK_COMBO) tc = c[q].cur;
                                                              pm = TextFit::keyedBitmap(tc == 1 ? "mcmsurfres_BMP_153.png" : "mcmsurfres_BMP_157.png", 0xC0C0C0); }
              else if ((pageDlg == 1338 || pageDlg == 1339) && id == 690) pm = TextFit::keyedBitmap("mccoreres_BMP_5537.png", 0xC0C0C0);
              if (!pm.isNull()) { b->setProperty("coreText", b->text()); b->setText(QString()); b->setIcon(QIcon(pm)); b->setIconSize(pm.size()); } }
            if (k.live) connect(b, &QPushButton::clicked, this, [change, id] { change([id] { pf_set(id, 1); }); });
            ctls_.append({ 6, key, b, 0, 0 }); } break;
        case PFK_TRACK: { auto *s = mk<QSlider>(box, r); s->setOrientation(Qt::Horizontal); s->setEnabled(false); } break;
        }
        { QWidget *last = box->children().isEmpty() ? nullptr : qobject_cast<QWidget *>(box->children().last()); made = last != before ? last : nullptr; }
        if (made && k.nd == 1) ndMark(made, k);          /* user decision UI-3: Mastercam's text, the marker as colour + tooltip */
        if (made && k.kind == PFK_STATIC && k.ours && k.tip[0]) { made->hide(); made->resize(0, 0); oursMarks << i; }   /* "(ours)": its control marked below */
        QString its; for (int q = 0; q < k.nitems && q < PF_MAXITEMS; q++) { its += QString::fromUtf8(k.items[q]); its += QChar('\n'); }
        tmpl_[i] = TmplCtl{ k.kind, k.id, k.x, k.y, k.w, k.h, k.live, k.nd, k.ours, k.nitems, !(k.cur >= 0 && k.cur < k.nitems),
                            QString::fromUtf8(k.pic), QString::fromUtf8(k.tip), its, made };
    }
    /* user decision UI-6 (2026-11-17): the few template controls whose Mastercam text needs a few px more in the dialog font (Wine
       Tahoma, ~3 % wider than MS Sans Serif 8) than the template gives, nudged by those pixels (formats/user_decisions.md UI-6) */
    for (int i = 0; i < n; i++) {
        QWidget *w = tmpl_[i].wid; if (!w) continue;
        for (const PfNudge &u : PF_NUDGE)
            if (c[i].kind == u.kind && c[i].id == u.id && c[i].w == u.w && c[i].h == u.h && !strncmp(c[i].text, u.text, strlen(u.text))) {
                QRect g = w->geometry(); w->setGeometry(g.x() + u.dx, g.y() + u.dy, g.width() + u.dw, g.height()); }
    }
    for (QGroupBox *g : groups) g->lower();             /* frames under their controls */
    /* user decision UI-3: an "(ours)" marker colours the control to its left on its row (the closest) and gives it the note as
       tooltip; with none, the marker static is shown as before */
    for (int m : oursMarks) {
        const PfShown &k = c[m]; int best = -1, bx = -1000000;
        for (int j = 0; j < n; j++) {
            const PfShown &q = c[j]; if (j == m || !tmpl_[j].wid || q.kind == PFK_GROUP || q.kind == PFK_CGROUP || q.kind == PFK_PIC || (q.kind == PFK_STATIC && q.ours)) continue;
            if (q.y > k.y + k.h || q.y + std::max(q.h, 10) < k.y) continue;
            if (q.x + q.w > k.x + 6 || q.x + q.w <= bx) continue;
            best = j; bx = q.x + q.w;
        }
        if (best < 0) {                                /* a page-wide note ("Unverified (ours)"): the nearest control (rectangle distance) */
            long bd = -1;
            for (int j = 0; j < n; j++) {
                const PfShown &q = c[j]; if (j == m || !tmpl_[j].wid || q.kind == PFK_GROUP || q.kind == PFK_CGROUP || q.kind == PFK_PIC || (q.kind == PFK_STATIC && q.ours)) continue;
                long dx = std::max({ 0, q.x - (k.x + k.w), k.x - (q.x + q.w) }), dy = std::max({ 0, q.y - (k.y + k.h), k.y - (q.y + q.h) });
                if (bd < 0 || dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = j; }
            }
        }
        if (best >= 0) TextFit::markOurs(tmpl_[best].wid, QString::fromUtf8(k.tip));
        else if (QWidget *w = tmpl_[m].wid) { QRect r = du(k.x, k.y, k.w, k.h); w->setGeometry(r.adjusted(0, -1, 0, 3)); w->show(); }
    }
}
/* perf: the controls of a refresh against the page shown: the same when every control keeps its kind, id, rectangle,
   live / NOT DECODED / ours state, picture, tooltip, items and whether a combo / list has a current item (what
   buildTemplate decides the widget by); only the values (texts, check / radio / group states, current items) differ */
bool OpDialog::tmplSame(const PfShown *c, int n) const {
    if (n != tmpl_.size()) return false;
    for (int i = 0; i < n; i++) {
        const PfShown &k = c[i]; const TmplCtl &t = tmpl_[i];
        if (!t.wid || k.kind != t.kind || k.id != t.id || k.x != t.x || k.y != t.y || k.w != t.w || k.h != t.h || k.live != t.live || k.nd != t.nd
            || k.ours != t.ours || k.nitems != t.nitems || !(k.cur >= 0 && k.cur < k.nitems) != (t.curNeg != 0)) return false;
        if (QString::fromUtf8(k.pic) != t.pic || QString::fromUtf8(k.tip) != t.tip) return false;
        QString its; for (int q = 0; q < k.nitems && q < PF_MAXITEMS; q++) { its += QString::fromUtf8(k.items[q]); its += QChar('\n'); }
        if (its != t.items) return false;
        if (k.kind == PFK_COMBO && !(k.cur >= 0 && k.cur < k.nitems)) return false;   /* buildTemplate's extra item with the shown text: rebuilt */
    }
    return true;
}
/* the values as buildTemplate sets them on fresh widgets (signals blocked: a value set here is not a user change) */
void OpDialog::tmplUpdate(const PfShown *c, int n) {
    for (int i = 0; i < n; i++) {
        const PfShown &k = c[i]; QWidget *w = tmpl_[i].wid; if (!w) continue; const QSignalBlocker blk(w);
        tmplShown_[i] = QString::fromUtf8(k.text); tmplOn_[i] = k.on;
        const QString KT = k.nd == 1 ? ndStrip(QString::fromUtf8(k.text)) : QString::fromUtf8(k.text);   /* UI-3: Mastercam's text */
        switch (k.kind) {
        case PFK_STATIC: case PFK_PUSH: case PFK_GROUP: {
            QString t = KT;
            if (auto *l = qobject_cast<QLabel *>(w)) { if (l->text() != t) l->setText(t); }
            else if (auto *b = qobject_cast<QPushButton *>(w)) { if (b->icon().isNull() && b->text() != t) b->setText(t);   /* a bitmap button keeps its bitmap (UI-5) */
                if (k.kind == PFK_PUSH && k.id == 718 && !b->icon().isNull()) {   /* 999: the picture follows Tip comp 692 (FUN_1800dbdd0 l.144430-446) */
                    int tc = 0; for (int q = 0; q < n; q++) if (c[q].id == 692 && c[q].kind == PFK_COMBO) tc = c[q].cur;
                    QPixmap pm = TextFit::keyedBitmap(tc == 1 ? "mcmsurfres_BMP_153.png" : "mcmsurfres_BMP_157.png", 0xC0C0C0);
                    if (!pm.isNull()) { b->setIcon(QIcon(pm)); b->setIconSize(pm.size()); } } }
            else if (auto *g = qobject_cast<QGroupBox *>(w)) { if (g->title() != t) g->setTitle(t); }
        } break;
        case PFK_CGROUP: if (auto *g = qobject_cast<QGroupBox *>(w)) { QString t = KT; if (g->title() != t) g->setTitle(t); g->setChecked(k.on != 0); } break;
        case PFK_EDIT: if (auto *e = qobject_cast<QLineEdit *>(w)) e->setText(KT); break;
        case PFK_COMBO: if (auto *cb = qobject_cast<QComboBox *>(w)) cb->setCurrentIndex(k.cur); break;
        case PFK_LIST: if (auto *l = qobject_cast<QListWidget *>(w)) { if (k.cur >= 0 && k.cur < k.nitems) l->setCurrentRow(k.cur); } break;
        case PFK_CHECK: case PFK_RADIO: if (auto *b = qobject_cast<QAbstractButton *>(w)) { QString t = KT; if (b->text() != t) b->setText(t); b->setChecked(k.on != 0); }
            else if (auto *g = qobject_cast<QGroupBox *>(w)) { if (g->title() != KT) g->setTitle(KT); g->setChecked(k.on != 0); } break;
        }
        if (k.nd == 1) ndMark(w, k);
    }
}

/* menu 1247 (core_ui otp_menu): Create new tool / Edit tool / Tool manager enable themselves with cui_hooks (phase 7) */
QMenu *OpDialog::toolMenu(const QPoint &global) {
    QMenu *m = new QMenu(this); m->setAttribute(Qt::WA_DeleteOnClose);
    OtpItem it[24]; int n = otp_menu(it, 24);
    for (int i = 0; i < n; i++) {
        QAction *a = m->addAction(QString::fromUtf8(it[i].text).replace('&', "&&")); int act = it[i].act;   /* '&' is text, not a mnemonic */
        a->setEnabled(it[i].en && act);
        if (it[i].en && act) connect(a, &QAction::triggered, this, [this, act] { otp_menu_act(act); refresh(); emit changed(); });
        if (it[i].sep) m->addSeparator();
    }
    m->popup(global);
    return m;
}

/* tests */
void OpDialog::dump() const {
    printf("    opdialog \"%s\" %dx%d\n", windowTitle().toUtf8().constData(), width(), height());
    std::function<void(QTreeWidgetItem *, int)> walk = [&](QTreeWidgetItem *it, int d) {
        printf("      tree %d %s%s%s\n", d, it->text(0).toUtf8().constData(), it->flags() & Qt::ItemIsSelectable ? "" : " (grey)", it->isSelected() ? " *" : "");
        for (int c = 0; c < it->childCount(); c++) walk(it->child(c), d + 1);
    };
    for (int i = 0; i < tree_->topLevelItemCount(); i++) walk(tree_->topLevelItem(i), 0);
    for (int i = 0; i < qv_->rowCount(); i++) printf("      qv %s = %s\n", qv_->item(i, 0)->text().toUtf8().constData(), qv_->item(i, 1)->text().toUtf8().constData());
    for (const auto &c : ctls_) {
        QString v;
        if (c.w->property("coreText").isValid() && !(qobject_cast<QAbstractButton *>(c.w) && static_cast<QAbstractButton *>(c.w)->isCheckable()) && !qobject_cast<QGroupBox *>(c.w)) {
            printf("      ctl %d %s = %s%s\n", c.kind, c.label.toUtf8().constData(), c.w->property("coreText").toString().toUtf8().constData(), c.w->isEnabled() ? "" : " (grey)");
            continue; }   /* UI-3: the widget shows Mastercam's text, the dump keeps the core's (marker included) */
        if (auto *le = qobject_cast<QLineEdit *>(c.w)) v = le->text();
        else if (auto *cb = qobject_cast<QAbstractButton *>(c.w)) v = cb->isCheckable() ? QString::number(cb->isChecked()) : cb->text();
        else if (auto *co = qobject_cast<QComboBox *>(c.w)) v = co->currentText();
        else if (auto *l = qobject_cast<QLabel *>(c.w)) v = l->text();
        else if (auto *t = qobject_cast<QTableWidget *>(c.w)) v = QString("%1 rows, row %2").arg(t->rowCount()).arg(t->currentRow());
        else if (auto *g = qobject_cast<QGroupBox *>(c.w)) v = QString::number(g->isChecked());
        else if (auto *lw = qobject_cast<QListWidget *>(c.w)) v = lw->currentItem() ? lw->currentItem()->text() : QString();
        else if (auto *e = qobject_cast<QPlainTextEdit *>(c.w)) v = e->toPlainText();
        printf("      ctl %d %s = %s%s\n", c.kind, c.label.toUtf8().constData(), v.toUtf8().constData(), c.w->isEnabled() ? "" : " (grey)");
    }
    printf("      status \"%s\"\n", status_->text().toUtf8().constData());
}
bool OpDialog::testSet(const QString &key, const QString &v) {
    if (key == "ok") { btn_[0]->click(); return true; }
    if (key == "cancel") { btn_[1]->click(); return true; }
    if (key == "apply") { if (!btn_[2]->isEnabled()) return false; btn_[2]->click(); return true; }   /* batch 12 */
    if (key == "tb") { for (int i = 0; i < PP_NTB; i++) if (PP_TB[i].bmp == v.toInt() && tb_[i]->isVisible()) { if (!tb_[i]->isEnabled()) return false; tb_[i]->click(); return true; } return false; }
    if (key == "tree") {                               /* tree=SLOT: a click on that page's item */
        QList<QTreeWidgetItem *> all = tree_->findItems("*", Qt::MatchWildcard | Qt::MatchRecursive);
        for (QTreeWidgetItem *it : all) if (it->data(0, Qt::UserRole).toInt() == v.toInt()) { emit tree_->itemClicked(it, 0); return true; }
        return false;
    }
    if (key == "tpsort") {                             /* tpsort=C: a click on the Tool page list's column C header */
        for (const auto &c : ctls_) if (c.label == "tp:25463") if (auto *t = qobject_cast<QTableWidget *>(c.w)) {
            int s = tpCols_.indexOf(v.toInt()); if (s < 0 || !t->isVisible()) return false;
            emit t->horizontalHeader()->sectionClicked(s); return true; }
        return false;
    }
    QString lab = key; int nth = 0;                    /* LABEL@N: the N-th control with that label (the Planes page's buttons) */
    if (key.contains('@')) { lab = key.section('@', 0, 0); nth = key.section('@', 1).toInt(); }
    for (const auto &c : ctls_) if (c.label == lab && c.w->isEnabled() && nth-- == 0) {
        if (auto *le = qobject_cast<QLineEdit *>(c.w)) { if (le->isReadOnly()) return false; le->setText(v); le->setModified(true); emit le->editingFinished(); return true; }
        if (auto *co = qobject_cast<QComboBox *>(c.w)) { co->setCurrentIndex(v.toInt()); emit co->activated(v.toInt()); return true; }
        if (auto *t = qobject_cast<QTableWidget *>(c.w)) { emit t->cellClicked(v.toInt(), 0); return true; }
        if (auto *l = qobject_cast<QListWidget *>(c.w)) { if (v.toInt() < 0 || v.toInt() >= l->count()) return false; emit l->itemClicked(l->item(v.toInt())); return true; }
        if (auto *g = qobject_cast<QGroupBox *>(c.w)) { g->setChecked(v.toInt() != 0); return true; }
        if (auto *e = qobject_cast<QPlainTextEdit *>(c.w)) { e->setPlainText(v); return true; }
        if (auto *b = qobject_cast<QAbstractButton *>(c.w)) {
            if (b->isCheckable()) { if (qobject_cast<QRadioButton *>(b)) { b->click(); } else b->setChecked(v.toInt() != 0); }
            else b->click();
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------------------------------------ Tool List 6410 */
/* core_ui TL6410 (CToolList, 440 x 231 du): the machine's library file (tooltip: the full path), the current library's
   tools (tl6410_prepare), OK = tl6410_ok (the tool to the part and the op), Cancel; New Library, Filter, Display mode,
   Help greyed (not ported). mcamui: a nuklear window (title, movable, no close box) centred on the window. */
ToolListDialog::ToolListDialog(QWidget *parent) : FloatDlg("Tool List", parent) {
    setClosable(false); setObjectName("toolList");
    setBodySize(du(0, 0, TL6410_W, TL6410_H).size());
    QWidget *b = body();
    for (int i = 0; i < TL6410_N; i++) {
        const TpCtl &k = TL6410[i]; QRect r = du(k.x, k.y, k.w, k.h);
        switch (k.kind) {
        case TPK_LABEL: { auto *l = mk<QLabel>(b, r); if (k.id == 10253) lib_ = l; else if (k.id == 3805) count_ = l; else l->setText(QString::fromUtf8(k.text)); } break;
        case TPK_BTN: { auto *p = mk<QPushButton>(b, r); p->setText(QString::fromUtf8(k.text)); p->setEnabled(k.ref == 0);
            if (k.id == 1) { ok_ = p; connect(p, &QPushButton::clicked, this, [this] { tl6410_ok(); emit closed(); }); }
            else if (k.id == 2) { cancel_ = p; connect(p, &QPushButton::clicked, this, [this] { tl6410_cancel(); emit closed(); }); } } break;
        case TPK_BMP: { auto *l = mk<QLabel>(b, QRect(r.x(), r.y(), 16, 16)); l->setPixmap(Icons::icon(QString("#file:") + k.text, 16, 16).pixmap(16, 16)); } break;
        case TPK_LIST: list_ = toolTable(b, r);   /* list 1762: the dialog's own CMillToolListCtrl (core_ui tl6410_rows: a row -> its library tool) */
            connect(list_, &QTableWidget::cellClicked, this, [this](int row, int) { if (!filling_ && row >= 0 && row < rows_.size()) tl6410_sel = rows_[row]; });
            connect(list_->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int s) {   /* LVN_COLUMNCLICK: 0x18086bdb0 -> SortList */
                if (filling_ || s < 0 || s >= cols_.size()) return;
                tl6410_sortlist(cols_[s]); refresh();
            }); break;
        case TPK_CHECK: { auto *c = mk<QCheckBox>(b, r.adjusted(0, -2, 0, 4)); c->setText(QString::fromUtf8(k.text)); c->setChecked(true); c->setEnabled(false); } break;
        case TPK_GROUP: { auto *g = mk<QGroupBox>(b, r); g->setTitle(QString::fromUtf8(k.text)); g->setEnabled(false); g->lower(); } break;
        case TPK_RADIO: { auto *rb = mk<QRadioButton>(b, r.adjusted(0, -2, 0, 4)); rb->setText(QString::fromUtf8(k.text)); rb->setAutoExclusive(false);
            rb->setEnabled(false); rb->setProperty("mode", k.id - 10408); radio_.append(rb); } break;
        }
    }
}
void ToolListDialog::refresh() {
    filling_ = true;
    tl6410_prepare();
    const char *m = tl6410_libfile(); lib_->setText(QString::fromUtf8(m));
    lib_->setToolTip(m[0] ? QString::fromUtf8(tlib_ok ? tlib.path : m) : QString());   /* UILabel::EnableDataTip */
    char cnt[64]; tl6410_count_text(cnt, sizeof cnt); count_->setText(QString::fromUtf8(cnt));
    for (QRadioButton *rb : radio_) rb->setChecked(tl6410_dispmode() == rb->property("mode").toInt());   /* FUN_18086d050: its own key */
    rows_.clear(); cols_.clear();
    if (tlib_ok) {
        int idx[4096], nr = tl6410_rows(idx, 4096), cols[TM_COLS], w[TM_COLS], nc = tl6410_cols(cols, w);
        for (int i = 0; i < nr; i++) rows_.append(idx[i]);
        for (int k = 0; k < nc; k++) cols_.append(cols[k]);
        fillToolRows(list_, 1, idx, nr, tl6410_sel, cols, w, nc, tl6410_set());
    } else list_->setRowCount(0);
    filling_ = false;
}
void ToolListDialog::dump() const {
    printf("    toollist lib \"%s\" %s rows %d sel %d\n", lib_->text().toUtf8().constData(), count_->text().toUtf8().constData(), list_->rowCount(), tl6410_sel);
    QHeaderView *h = list_->horizontalHeader();       /* the header as shown (the sort indicator) and the rows (list = the library tool's index, * = selected) */
    printf("      list sort %s %s\n", h->isSortIndicatorShown() && list_->horizontalHeaderItem(h->sortIndicatorSection()) ? list_->horizontalHeaderItem(h->sortIndicatorSection())->text().toUtf8().constData() : "-",
           h->sortIndicatorOrder() == Qt::AscendingOrder ? "ascending" : "descending");
    for (int r = 0; r < list_->rowCount() && r < rows_.size(); r++) {
        printf("      list %d%s:", rows_[r], rows_[r] == tl6410_sel ? "*" : "");
        for (int k = 0; k < list_->columnCount(); k++) printf(" [%s]", list_->item(r, k) ? list_->item(r, k)->text().toUtf8().constData() : "");
        printf("\n");
    }
}
bool ToolListDialog::testSet(const QString &key, const QString &v) {
    if (key == "sort") { int s = cols_.indexOf(v.toInt()); if (s < 0) return false; emit list_->horizontalHeader()->sectionClicked(s); return true; }
    if (key == "row") { int r = v.toInt(); if (r < 0 || r >= list_->rowCount()) return false; list_->selectRow(r); emit list_->cellClicked(r, 0); return true; }
    if (key == "ok" && ok_) { ok_->click(); return true; }
    if (key == "cancel" && cancel_) { cancel_->click(); return true; }
    return false;
}

/* ------------------------------------------------------------------------------------------------ the Planes page's dialogs */
/* core_ui cui_planes.c: Plane Selection 842 (SelectPlaneFromList: the plane names, the current one preselected; OK = pls_ok,
   Cancel = pls_cancel; "Select plane icon" / "Set new origin" / Help greyed) and Update Plane 10269 (DoModalWorkOffset:
   the message, choices 0 / 1 greyed - NOT PORTED -, OK / Cancel = upl_close). mcamui: opparams_ui.inc draw_planes_dialogs. */
PlanesDialog::PlanesDialog(QWidget *parent, int upl) : FloatDlg(upl ? "Update Plane" : "Plane Selection", parent), upl_(upl) {
    setClosable(false); setObjectName(upl ? "updatePlane" : "planeSelection");
    const TpCtl *T = upl ? UPL10269 : PLS842; int n = upl ? UPL10269_N : PLS842_N;
    setBodySize(du(0, 0, upl ? UPL10269_W : PLS842_W, upl ? UPL10269_H : PLS842_H).size());
    QWidget *b = body();
    for (int i = 0; i < n; i++) {
        const TpCtl &k = T[i]; QRect r = du(k.x, k.y, k.w, k.h);
        switch (k.kind) {
        case TPK_LIST: list_ = new QTableWidget(b); dlgFont(list_); list_->setGeometry(r); list_->setColumnCount(1);
            list_->setHorizontalHeaderLabels({ "Name" });   /* PlanesListCtrl columns NOT DECODED: names */
            list_->horizontalHeader()->setStretchLastSection(true); list_->verticalHeader()->hide();
            list_->setSelectionBehavior(QAbstractItemView::SelectRows); list_->setSelectionMode(QAbstractItemView::SingleSelection);
            list_->setEditTriggers(QAbstractItemView::NoEditTriggers); list_->show();
            connect(list_, &QTableWidget::cellClicked, this, [this](int row, int) { if (!filling_) pls_sel = row; }); break;
        case TPK_LABEL: text_ = mk<QLabel>(b, r); text_->setWordWrap(true); text_->setAlignment(Qt::AlignLeft | Qt::AlignTop); break;
        case TPK_BTN: { auto *p = mk<QPushButton>(b, r); p->setText(QString::fromUtf8(k.text)); p->setEnabled(k.ref == 0);
            if (k.id == 1) ok_ = p; else if (k.id == 2) cancel_ = p;
            if (k.id == 1) connect(p, &QPushButton::clicked, this, [this] { if (upl_) upl_close(1); else pls_ok(); emit closed(); });
            else if (k.id == 2) connect(p, &QPushButton::clicked, this, [this] { if (upl_) upl_close(0); else pls_cancel(); emit closed(); }); } break;
        case TPK_CHECK: { auto *c = mk<QCheckBox>(b, r.adjusted(0, -2, 0, 4)); c->setText(QString::fromUtf8(k.text)); c->setEnabled(false); } break;
        case TPK_RADIO: { auto *rb = mk<QRadioButton>(b, r.adjusted(0, -2, 0, 4)); rb->setText(QString::fromUtf8(k.text)); rb->setAutoExclusive(false);
            rb->setEnabled(k.ref == 0); int c = k.id - 10281; radio_[c] = rb;
            connect(rb, &QRadioButton::clicked, this, [this, c] { if (!filling_) upl_choice = c; refresh(); }); } break;
        }
    }
}
void PlanesDialog::refresh() {
    filling_ = true;
    if (upl_) { text_->setText(QString::fromUtf8(upl_text)); for (int c = 0; c < 3; c++) if (radio_[c]) radio_[c]->setChecked(upl_choice == c);
                if (ok_) ok_->setEnabled(upl_ok_enabled()); }   /* choice 2 only (core_ui upl_ok_enabled) */
    else {
        list_->setRowCount(pls_rows());
        for (int i = 0; i < pls_rows(); i++) list_->setItem(i, 0, new QTableWidgetItem(QString::fromUtf8(pls_row(i))));
        list_->clearSelection(); if (pls_sel >= 0 && pls_sel < pls_rows()) list_->selectRow(pls_sel);
    }
    filling_ = false;
}
bool PlanesDialog::testSet(const QString &key, const QString &v) {
    if (key == "ok" && ok_) { if (!ok_->isEnabled()) return false; ok_->click(); return true; }
    if (key == "cancel" && cancel_) { cancel_->click(); return true; }
    if (key == "row" && list_ && v.toInt() >= 0 && v.toInt() < list_->rowCount()) { list_->selectRow(v.toInt()); emit list_->cellClicked(v.toInt(), 0); return true; }
    if (key == "choice" && upl_ && v.toInt() >= 0 && v.toInt() < 3 && radio_[v.toInt()] && radio_[v.toInt()]->isEnabled()) { radio_[v.toInt()]->click(); return true; }
    return false;
}
void PlanesDialog::dump() const {
    if (upl_) printf("    updateplane \"%s\" choice %d\n", text_->text().toUtf8().constData(), upl_choice);
    else printf("    planeselect rows %d sel %d\n", list_->rowCount(), list_->currentRow());
}
