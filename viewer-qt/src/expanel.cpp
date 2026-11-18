#include "expanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QCheckBox>
#include <QPushButton>
#include <QToolButton>
#include <QListWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QScrollArea>
#include <QMenu>
#include <QApplication>
#include <QRegularExpressionValidator>
#include <cstdio>
#include <cstring>
#include "core_ui/core_ui.h"
extern bool g_qt_scripting;                           /* mainwindow.cpp */

/* the texts are mcamui.c ex_page_basic / ex_page_advanced's: the BAML texts without their access-key underscores (access
   keys are not implemented); icon-only buttons are labelled with their tooltip titles (resx
   MCGeomSld.UI.Solids.Extrude.SolidsExtrudeManagerViewToolTips). Colours as mcamui: an expander's text rgb(30,50,135), a
   validation error rgb(200,30,30) (label + 1.5 px border, its text as the tooltip), a disabled control greyed. Numbers
   as nuklear's property box shows them: 2 decimals (vendor/nuklear.h NK_MAX_FLOAT_PRECISION); the EvaluatorBox's own
   format is NOT DECODED. */
static const char *ERR_CSS = "border: 1.5px solid rgb(200,30,30);";
static QString num(double v) { return QString::asprintf("%.2f", v); }
static void setErr(QWidget *w, const char *err, QLabel *lab = nullptr) {
    w->setStyleSheet(err ? QString("%1 { %2 }").arg(w->metaObject()->className(), ERR_CSS) : QString());
    w->setToolTip(err ? QString::fromUtf8(err) : QString());
    if (lab) { lab->setStyleSheet(err ? "color: rgb(200,30,30);" : ""); lab->setToolTip(w->toolTip()); }
}

ExtrudePanel::Exp ExtrudePanel::expander(QVBoxLayout *page, const char *text, int ix, bool checkHeader) {
    Exp e; e.ix = ix; e.check = nullptr;
    e.hdr = new QWidget; auto *hl = new QHBoxLayout(e.hdr); hl->setContentsMargins(0, 4, 0, 0); hl->setSpacing(4);
    e.arrow = new QToolButton; e.arrow->setAutoRaise(true); e.arrow->setFixedSize(22, 22); hl->addWidget(e.arrow);
    if (checkHeader) { e.check = new QCheckBox(text); hl->addWidget(e.check, 1); }   /* HasCheckBox: checking it opens it */
    else { auto *l = new QLabel(text); l->setStyleSheet("color: rgb(30,50,135);"); hl->addWidget(l, 1); }
    e.box = new QWidget; auto *bl = new QVBoxLayout(e.box); bl->setContentsMargins(0, 0, 0, 0); bl->setSpacing(2);
    page->addWidget(e.hdr); page->addWidget(e.box);
    connect(e.arrow, &QToolButton::clicked, this, [this, ix] { ex_exp[ix] = !ex_exp[ix]; refresh(); });
    exps_ << e;
    return e;
}
/* an EvaluatorBox row: label column (110 px, mcamui ex_value) + the value, clamped like nk_property_double */
QLineEdit *ExtrudePanel::value(QVBoxLayout *box, const char *label, QLabel **lab, double lo, double hi, double *var) {
    QWidget *row = new QWidget; auto *hl = new QHBoxLayout(row); hl->setContentsMargins(0, 0, 0, 0); hl->setSpacing(4);
    *lab = new QLabel(label); (*lab)->setFixedWidth(110); hl->addWidget(*lab);
    auto *le = new QLineEdit; hl->addWidget(le, 1);
    connect(le, &QLineEdit::editingFinished, this, [this, le, lo, hi, var] {
        if (filling_ || !le->isModified()) return;
        le->setModified(false);
        bool ok; double v = le->text().toDouble(&ok);
        if (ok) *var = v < lo ? lo : v > hi ? hi : v;
        edited();
    });
    box->addWidget(row);
    return le;
}

ExtrudePanel::ExtrudePanel(QWidget *parent) : QWidget(parent) {
    auto *v = new QVBoxLayout(this); v->setContentsMargins(4, 4, 4, 4); v->setSpacing(4);
    /* the two tabs, both enabled by EnableBasicTab (off only during an arrow drag) */
    tabs_ = new QTabBar; tabs_->addTab("Basic"); tabs_->addTab("Advanced"); tabs_->setExpanding(false); v->addWidget(tabs_);
    pages_ = new QStackedWidget; v->addWidget(pages_, 1);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int i) { if (filling_) return; ex_tab = i; refresh(); });
    QVBoxLayout *pg[2];
    for (int i = 0; i < 2; i++) {
        auto *sa = new QScrollArea; sa->setWidgetResizable(true); sa->setFrameShape(QFrame::NoFrame);
        auto *w = new QWidget; pg[i] = new QVBoxLayout(w); pg[i]->setContentsMargins(0, 0, 0, 0); pg[i]->setSpacing(2);
        sa->setWidget(w); pages_->addWidget(sa);
    }
    auto line = [](QVBoxLayout *b) { QWidget *r = new QWidget; auto *h = new QHBoxLayout(r); h->setContentsMargins(0, 0, 0, 0); h->setSpacing(4); b->addWidget(r); return h; };
    auto bl = [](const Exp &e) { return static_cast<QVBoxLayout *>(e.box->layout()); };

    /* ---- Basic: Operation (icon ImageExtrude16) */
    Exp e = expander(pg[0], "Operation", 0, false);
    { QHBoxLayout *h = line(bl(e)); nameLab_ = new QLabel("Name:"); nameLab_->setFixedWidth(60); h->addWidget(nameLab_); name_ = new QLineEdit; name_->setMaxLength(63); h->addWidget(name_, 1); }
    connect(name_, &QLineEdit::textEdited, this, [this](const QString &t) { snprintf(ex_name, sizeof ex_name, "%s", t.toUtf8().constData()); edited(); });
    create_ = new QRadioButton("Create body"); cut_ = new QRadioButton("Cut body"); boss_ = new QRadioButton("Add boss");
    for (auto *r : { create_, cut_, boss_ }) { r->setAutoExclusive(false); bl(e)->addWidget(r); }
    connect(create_, &QRadioButton::clicked, this, [this] { ex_set_type(EX_CREATE); edited(); });
    connect(cut_, &QRadioButton::clicked, this, [this] { ex_set_type(EX_CUT); edited(); });
    connect(boss_, &QRadioButton::clicked, this, [this] { ex_set_type(EX_BOSS); edited(); });
    { QHBoxLayout *h = line(bl(e)); targetLab_ = new QLabel("Target"); targetLab_->setFixedWidth(60); h->addWidget(targetLab_);
      target_ = new QLineEdit; target_->setReadOnly(true); h->addWidget(target_, 1);   /* TargetBodyName, sld 858 "(None)" */
      selTarget_ = new QPushButton("Select Target Body"); selTarget_->setFixedWidth(130); h->addWidget(selTarget_); }
    connect(selTarget_, &QPushButton::clicked, this, [this] { ex_btn_select_target(); edited(); });
    single_ = new QCheckBox("Create a single operation"); autoOp_ = new QCheckBox("Automatically determine operation type");
    bl(e)->addWidget(single_); bl(e)->addWidget(autoOp_);
    connect(single_, &QCheckBox::clicked, this, [this](bool on) { ex_single = on; edited(); });
    connect(autoOp_, &QCheckBox::clicked, this, [this](bool on) { ex_auto_op = on; edited(); });

    /* Chains (icon ImageChains16): items "Chain  N", extended selection, context menu Remove / Reverse (greyed with no
       selection) / Add / Rechain all; Reverse All (model 0x2c0 toggles P+0x39), Add Chains (0x321, 1), Rechain All
       (clears the list, 0x321, 0) */
    e = expander(pg[0], "Chains", 1, false);
    chains_ = new QListWidget; chains_->setFixedHeight(96); chains_->setSelectionMode(QAbstractItemView::NoSelection);
    chains_->setContextMenuPolicy(Qt::CustomContextMenu); bl(e)->addWidget(chains_);
    connect(chains_, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        int i = chains_->row(it); bool ctrl = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
        ex_chain_click(i, ctrl, i >= 0 && i < 256 ? !ex_chsel[i] : 0); edited(); });
    connect(chains_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        QMenu m(this); bool any = ex_any_sel() != 0;
        m.addAction("Remove", this, [this] { ex_remove_sel(); edited(); })->setEnabled(any);
        m.addAction("Reverse", this, [this] { ex_reverse_sel(); edited(); })->setEnabled(any);
        m.addAction("Add", this, [this] { ex_add_chains(0); edited(); });
        m.addAction("Rechain all", this, [this] { ex_add_chains(1); edited(); });
        m.exec(chains_->mapToGlobal(p)); });
    { QHBoxLayout *h = line(bl(e)); revAll_ = new QPushButton("Reverse All"); addCh_ = new QPushButton("Add Chains"); rechain_ = new QPushButton("Rechain All");
      h->addWidget(revAll_); h->addWidget(addCh_); h->addWidget(rechain_); }
    connect(revAll_, &QPushButton::clicked, this, [this] { ex_p.reverse = !ex_p.reverse; edited(); });
    connect(addCh_, &QPushButton::clicked, this, [this] { ex_add_chains(0); edited(); });
    connect(rechain_, &QPushButton::clicked, this, [this] { ex_add_chains(1); edited(); });

    /* Distance (icon ImageMeasure16): Distance + its value + AutoCursor (0x322); Through all (enabled = Cut body);
       Both directions */
    e = expander(pg[0], "Distance", 2, false);
    { QHBoxLayout *h = line(bl(e)); mDist_ = new QRadioButton("Distance"); mDist_->setAutoExclusive(false); mDist_->setFixedWidth(90); h->addWidget(mDist_);
      dist_ = new QLineEdit; h->addWidget(dist_, 1); autoCur_ = new QPushButton("AutoCursor"); autoCur_->setFixedWidth(90); h->addWidget(autoCur_); }
    connect(mDist_, &QRadioButton::clicked, this, [this] { ex_p.method = EXM_DISTANCE; edited(); });
    connect(dist_, &QLineEdit::editingFinished, this, [this] {
        if (filling_ || !dist_->isModified()) return; dist_->setModified(false);
        bool ok; double d = dist_->text().toDouble(&ok); if (ok) ex_p.dist = d < 0 ? 0 : d > 1e7 ? 1e7 : d; edited(); });
    connect(autoCur_, &QPushButton::clicked, this, [this] { ex_btn_autocursor(); edited(); });
    mThrough_ = new QRadioButton("Through all"); mThrough_->setAutoExclusive(false); both_ = new QCheckBox("Both directions");
    bl(e)->addWidget(mThrough_); bl(e)->addWidget(both_);
    connect(mThrough_, &QRadioButton::clicked, this, [this] { ex_set_through_all(); edited(); });
    connect(both_, &QCheckBox::clicked, this, [this](bool on) { ex_p.both = on; edited(); });

    /* Trim to Faces (checkbox header): the faces list ("Face N", sld 687) - this program's bodies have no pickable faces,
       so it stays empty and Add Selection / Reselect All are greyed (sld_array_pick, prompt sld 443: not available here) */
    e = expander(pg[0], "Trim to Faces", 3, true);
    connect(e.check, &QCheckBox::clicked, this, [this](bool on) { ex_set_check(3, on); edited(); });
    faces_ = new QListWidget; faces_->setFixedHeight(60); faces_->setSelectionMode(QAbstractItemView::NoSelection); bl(e)->addWidget(faces_);
    { QHBoxLayout *h = line(bl(e)); addSel_ = new QPushButton("Add Selection"); reselAll_ = new QPushButton("Reselect All");
      addSel_->setEnabled(false); reselAll_->setEnabled(false); h->addWidget(addSel_); h->addWidget(reselAll_); }
    minDist_ = new QCheckBox("Calculate minimum distance to face"); bl(e)->addWidget(minDist_);   /* DAT_1803bab70, stored on change (new op) */
    connect(minDist_, &QCheckBox::clicked, this, [this](bool on) { if (ex_edit_idx < 0) ex_min_dist = on; edited(); });
    pg[0]->addStretch(1);

    /* ---- Advanced: Draft (checkbox header): Angle (EvaluatorBox StepValue 1.0, degrees), Reverse (= DraftOutward),
       Split (Draft and Both directions), Draft ends (Draft and Thin Wall) */
    e = expander(pg[1], "Draft", 4, true);
    connect(e.check, &QCheckBox::clicked, this, [this](bool on) { ex_set_check(4, on); edited(); });
    ang_ = value(bl(e), "Angle", &angLab_, -90, 90, &ex_p.draft_ang);
    draftRev_ = new QCheckBox("Reverse"); split_ = new QCheckBox("Split"); draftEnds_ = new QCheckBox("Draft ends");
    for (auto *c : { draftRev_, split_, draftEnds_ }) bl(e)->addWidget(c);
    connect(draftRev_, &QCheckBox::clicked, this, [this](bool on) { ex_p.draft_rev = on; edited(); });
    connect(split_, &QCheckBox::clicked, this, [this](bool on) { ex_p.split = on; edited(); });
    connect(draftEnds_, &QCheckBox::clicked, this, [this](bool on) { ex_p.draft_ends = on; edited(); });
    /* Thin Wall (checkbox header; a change posts 0x329: app+0x2e78 = 0): Direction: 1 / 2 / Both, the two thicknesses
       (EnableDirection1 / 2) */
    e = expander(pg[1], "Thin Wall", 5, true);
    connect(e.check, &QCheckBox::clicked, this, [this](bool on) { ex_set_check(5, on); edited(); });
    { auto *g = new QWidget; auto *gl = new QGridLayout(g); gl->setContentsMargins(0, 0, 0, 0); gl->setHorizontalSpacing(4); gl->setVerticalSpacing(2);
      dirLab_ = new QLabel("Direction:"); dirLab_->setFixedWidth(110); gl->addWidget(dirLab_, 0, 0);
      d1_ = new QRadioButton("Direction 1"); d2_ = new QRadioButton("Direction 2"); dBoth_ = new QRadioButton("Both");
      gl->addWidget(d1_, 0, 1); gl->addWidget(d2_, 1, 1); gl->addWidget(dBoth_, 2, 1); bl(e)->addWidget(g); }
    for (auto *r : { d1_, d2_, dBoth_ }) r->setAutoExclusive(false);
    connect(d1_, &QRadioButton::clicked, this, [this] { ex_p.thin_dir = THIN_DIR1; edited(); });
    connect(d2_, &QRadioButton::clicked, this, [this] { ex_p.thin_dir = THIN_DIR2; edited(); });
    connect(dBoth_, &QRadioButton::clicked, this, [this] { ex_p.thin_dir = THIN_BOTH; edited(); });
    t1_ = value(bl(e), "Direction 1:", &t1Lab_, 0, 1e6, &ex_p.thick);
    t2_ = value(bl(e), "Direction 2:", &t2Lab_, 0, 1e6, &ex_p.thick2);
    /* Plane Orientation (icon ImagePlaneOrientation16): the Vector box (MaskedTextBox: digits, '.', '-', ','), Set
       Normal to chain (FUN_1801329f0), Set C-Plane + (FUN_180132930), Select Vector (sld 905) */
    e = expander(pg[1], "Plane Orientation", 6, false);
    vec_ = new QLineEdit; vec_->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9.,\\-]*"), vec_)); bl(e)->addWidget(vec_);
    connect(vec_, &QLineEdit::editingFinished, this, [this] {
        if (filling_) return;
        snprintf(ex_vec_txt, sizeof ex_vec_txt, "%s", vec_->text().toUtf8().constData()); ex_vec_commit(); edited(); });
    { QHBoxLayout *h = line(bl(e)); setNormal_ = new QPushButton("Set Normal to chain"); setCplane_ = new QPushButton("Set C-Plane +"); selVec_ = new QPushButton("Select Vector");
      h->addWidget(setNormal_); h->addWidget(setCplane_); h->addWidget(selVec_); }
    connect(setNormal_, &QPushButton::clicked, this, [this] { ex_set_normal(); edited(); });
    connect(setCplane_, &QPushButton::clicked, this, [this] { ex_set_cplane(); edited(); });
    connect(selVec_, &QPushButton::clicked, this, [this] { ex_btn_select_vector(); edited(); });
    /* Preview (icon ImageAutoPreview16): Automatically preview results; the Preview button (enabled = !Preview, posts 0x2bf) */
    e = expander(pg[1], "Preview", 7, false);
    autoPrev_ = new QCheckBox("Automatically preview results"); bl(e)->addWidget(autoPrev_);
    prevBtn_ = new QPushButton("Preview"); prevBtn_->setFixedWidth(90); bl(e)->addWidget(prevBtn_);
    connect(autoPrev_, &QCheckBox::clicked, this, [this](bool on) { ex_prev = on; edited(); });
    connect(prevBtn_, &QPushButton::clicked, this, [this] { ex_prev_req = 1; edited(); });
    pg[1]->addStretch(1);

    /* FunctionPanel: OK (0x2bd), OK-New = Apply (0x2c0) / OK-Regen in edit mode (its label is the shared FunctionPanel
       resource, NOT DECODED: "Regen" as mcamui), Cancel (0x2be), Help (the topic is NOT DECODED: greyed); OK / Apply /
       Cancel off during an arrow drag. At the bottom of the panel, as mcamui draws it. */
    auto *br = new QHBoxLayout; br->setSpacing(4);
    ok_ = new QPushButton("OK"); apply_ = new QPushButton("Apply"); cancel_ = new QPushButton("Cancel"); help_ = new QPushButton("Help"); help_->setEnabled(false);
    for (auto *b : { ok_, apply_, cancel_, help_ }) { b->setFixedHeight(28); br->addWidget(b); }
    connect(ok_, &QPushButton::clicked, this, [this] { emit button(0); });
    connect(apply_, &QPushButton::clicked, this, [this] { emit button(1); });
    connect(cancel_, &QPushButton::clicked, this, [this] { emit button(2); });
    v->addLayout(br);
}

void ExtrudePanel::edited() { refresh(); emit changed(); }

void ExtrudePanel::refresh() {
    if (!ex_mode || !ex_phase) return;
    filling_ = true;
    ExEn en; ex_enables(&en);
    const char *err[EXV_N]; ex_validate(err);
    tabs_->setCurrentIndex(ex_tab); pages_->setCurrentIndex(ex_tab);
    tabs_->setTabEnabled(0, en.tabs); tabs_->setTabEnabled(1, en.tabs);
    for (const Exp &e : exps_) {
        e.arrow->setArrowType(ex_exp[e.ix] ? Qt::DownArrow : Qt::RightArrow); e.box->setVisible(ex_exp[e.ix] != 0);
        if (e.check) {
            int v = e.ix == 3 ? ex_p.trim : e.ix == 4 ? ex_p.draft : ex_p.thin;
            QSignalBlocker b(e.check); e.check->setChecked(v != 0);
            e.check->setEnabled(e.ix == 3 ? en.trim_hdr != 0 : true);
        }
    }
    auto chk = [](QAbstractButton *b, bool on, bool enabled) { QSignalBlocker k(b); b->setChecked(on); b->setEnabled(enabled); };
    auto txt = [](QLineEdit *le, const QString &t, bool enabled) { if (!le->hasFocus() || !enabled) { QSignalBlocker k(le); le->setText(t); } le->setEnabled(enabled); };
    /* Operation */
    if (!name_->hasFocus()) { QSignalBlocker k(name_); name_->setText(QString::fromUtf8(ex_name)); }
    setErr(name_, err[EXV_NAME], nameLab_);
    chk(create_, ex_p.type == EX_CREATE, en.create); chk(cut_, ex_p.type == EX_CUT, en.cut); chk(boss_, ex_p.type == EX_BOSS, en.boss);
    targetLab_->setEnabled(en.target);
    target_->setText(en.has_t && ex_p.target < nsbodies ? QString::fromUtf8(sbodies[ex_p.target].name) : QString("(None)"));
    target_->setEnabled(en.target);
    selTarget_->setEnabled(en.sel_target);
    chk(single_, ex_single != 0, en.single); chk(autoOp_, ex_auto_op != 0, en.auto_op);
    /* Chains */
    if (chains_->count() != ncchains) {
        chains_->clear();
        for (int i = 0; i < ncchains; i++) { char t[64]; ex_chain_label(i, t, sizeof t); chains_->addItem(QString::fromUtf8(t)); }
    }
    for (int i = 0; i < ncchains && i < 256; i++) { QListWidgetItem *it = chains_->item(i);
        it->setBackground(ex_chsel[i] ? chains_->palette().highlight() : QBrush()); it->setForeground(ex_chsel[i] ? chains_->palette().highlightedText() : QBrush()); }
    setErr(chains_, err[EXV_CHAINS]);
    /* Distance */
    chk(mDist_, ex_p.method == EXM_DISTANCE, true);
    txt(dist_, num(ex_p.dist), true); setErr(dist_, err[EXV_DIST]);
    chk(mThrough_, ex_p.method == EXM_THROUGH_ALL, en.through_all);
    chk(both_, ex_p.both != 0, true);
    /* Trim to Faces */
    setErr(faces_, err[EXV_FACES]);
    chk(minDist_, en.edit ? false : ex_min_dist != 0, en.min_dist);   /* edit: reads false (model 0x128) */
    /* Draft */
    txt(ang_, num(ex_p.draft_ang), en.draft_ctl); angLab_->setEnabled(en.draft_ctl); setErr(ang_, err[EXV_ANGLE], angLab_);
    chk(draftRev_, ex_p.draft_rev != 0, en.draft_ctl); chk(split_, ex_p.split != 0, en.split); chk(draftEnds_, ex_p.draft_ends != 0, en.draft_ends);
    /* Thin Wall */
    dirLab_->setEnabled(en.thin_ctl);
    chk(d1_, ex_p.thin_dir == THIN_DIR1, en.thin_ctl); chk(d2_, ex_p.thin_dir == THIN_DIR2, en.thin_ctl); chk(dBoth_, ex_p.thin_dir == THIN_BOTH, en.thin_ctl);
    txt(t1_, num(ex_p.thick), en.d1); t1Lab_->setEnabled(en.d1); setErr(t1_, err[EXV_D1], t1Lab_);
    txt(t2_, num(ex_p.thick2), en.d2); t2Lab_->setEnabled(en.d2); setErr(t2_, err[EXV_D2], t2Lab_);
    /* Plane Orientation */
    if (!vec_->hasFocus()) {                           /* the shown value (FUN_180132ab0) is written to ex_vec_txt as mcamui's frame does - not
                                                          during a scripted step (mcamui draws no frame between steps) */
        char keep[sizeof ex_vec_txt]; memcpy(keep, ex_vec_txt, sizeof keep);
        ex_vec_show(); QSignalBlocker k(vec_); vec_->setText(QString::fromUtf8(ex_vec_txt));
        if (g_qt_scripting || !(ex_tab == 1 && ex_exp[6])) memcpy(ex_vec_txt, keep, sizeof keep);   /* mcamui: only while the Advanced tab's Plane Orientation is drawn */
    }
    vec_->setStyleSheet(ex_vec_bad ? QString("QLineEdit { %1 }").arg(ERR_CSS) : QString());
    /* Preview */
    chk(autoPrev_, ex_prev != 0, true); prevBtn_->setEnabled(en.preview_btn);
    /* buttons */
    apply_->setText(en.edit ? "Regen" : "Apply");
    ok_->setEnabled(en.buttons); apply_->setEnabled(en.buttons); cancel_->setEnabled(en.buttons);
    filling_ = false;
}

/* ---- tests: the controls (text, value, enabled) and a change through a widget (its signal = the user's edit) */
void ExtrudePanel::dump() const {
    auto b = [](const QAbstractButton *w) { return QString("%1%2%3").arg(w->text(), w->isCheckable() ? (w->isChecked() ? " on" : " off") : "", w->isEnabled() ? "" : " (greyed)"); };
    auto l = [](const char *n, const QLineEdit *w) { return QString("%1 '%2'%3%4").arg(n, w->text(), w->isEnabled() ? "" : " (greyed)", w->toolTip().isEmpty() ? "" : " error: " + w->toolTip()); };
    QStringList o;
    o << QString("tab %1 (%2)").arg(tabs_->tabText(tabs_->currentIndex()), tabs_->isTabEnabled(0) ? "tabs live" : "tabs greyed");
    for (const Exp &e : exps_) o << QString("expander %1 %2%3").arg(e.ix).arg(ex_exp[e.ix] ? "open" : "closed").arg(e.check ? " " + b(e.check) : "");
    o << l("name", name_) << b(create_) << b(cut_) << b(boss_) << l("target", target_) << b(selTarget_) << b(single_) << b(autoOp_);
    QString ch; for (int i = 0; i < chains_->count(); i++) ch += QString(" [%1%2]").arg(chains_->item(i)->text(), i < 256 && ex_chsel[i] ? " sel" : "");
    o << "chains" + ch + (chains_->toolTip().isEmpty() ? "" : " error: " + chains_->toolTip());
    o << b(revAll_) << b(addCh_) << b(rechain_) << b(mDist_) << l("dist", dist_) << b(autoCur_) << b(mThrough_) << b(both_);
    o << QString("faces%1").arg(faces_->toolTip().isEmpty() ? "" : " error: " + faces_->toolTip()) << b(addSel_) << b(reselAll_) << b(minDist_);
    o << l("angle", ang_) << b(draftRev_) << b(split_) << b(draftEnds_) << b(d1_) << b(d2_) << b(dBoth_) << l("t1", t1_) << l("t2", t2_);
    o << l("vector", vec_) << b(setNormal_) << b(setCplane_) << b(selVec_) << b(autoPrev_) << b(prevBtn_) << b(ok_) << b(apply_) << b(cancel_) << b(help_);
    printf("    expanel:\n"); for (const QString &s : o) printf("      %s\n", s.toUtf8().constData());
}
bool ExtrudePanel::testSet(const QString &k, const QString &v) {
    auto click = [](QAbstractButton *b) { if (!b->isEnabled()) return false; b->click(); return true; };
    auto type = [](QLineEdit *le, const QString &t) { if (!le->isEnabled()) return false; le->setText(t); le->setModified(true); emit le->editingFinished(); return true; };
    if (k == "tab") { tabs_->setCurrentIndex(v.toInt()); return true; }
    if (k == "exp") { int i = v.toInt(); for (const Exp &e : exps_) if (e.ix == i) { e.arrow->click(); return true; } return false; }
    if (k == "check") { int i = v.toInt(); for (const Exp &e : exps_) if (e.ix == i && e.check) return click(e.check); return false; }
    if (k == "name") { name_->setText(v); emit name_->textEdited(v); return true; }
    if (k == "create") return click(create_); if (k == "cut") return click(cut_); if (k == "boss") return click(boss_);
    if (k == "seltarget") return click(selTarget_); if (k == "single") return click(single_); if (k == "autoop") return click(autoOp_);
    if (k == "chain") { int i = v.toInt(); if (i < 0 || i >= chains_->count()) return false; emit chains_->itemClicked(chains_->item(i)); return true; }
    if (k == "reverseall") return click(revAll_); if (k == "addchains") return click(addCh_); if (k == "rechainall") return click(rechain_);
    if (k == "mdist") return click(mDist_); if (k == "dist") return type(dist_, v); if (k == "autocursor") return click(autoCur_);
    if (k == "through") return click(mThrough_); if (k == "both") return click(both_); if (k == "mindist") return click(minDist_);
    if (k == "angle") return type(ang_, v); if (k == "draftrev") return click(draftRev_); if (k == "split") return click(split_); if (k == "draftends") return click(draftEnds_);
    if (k == "d1") return click(d1_); if (k == "d2") return click(d2_); if (k == "dboth") return click(dBoth_);
    if (k == "t1") return type(t1_, v); if (k == "t2") return type(t2_, v);
    if (k == "vector") { vec_->setText(v); emit vec_->editingFinished(); return true; }
    if (k == "setnormal") return click(setNormal_); if (k == "setcplane") return click(setCplane_); if (k == "selvector") return click(selVec_);
    if (k == "autoprev") return click(autoPrev_); if (k == "preview") return click(prevBtn_);
    if (k == "ok") return click(ok_); if (k == "apply") return click(apply_); if (k == "cancel") return click(cancel_);
    return false;
}
