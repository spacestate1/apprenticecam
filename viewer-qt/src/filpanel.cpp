#include "filpanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QListWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QMenu>
#include <cstdio>
#include "core_ui/core_ui.h"

/* texts: the BAML's (solids/fillet/solidsfilletmanagerview.baml) without the access-key underscores, and the Solid Selection dialog's
   (mcgeomsldres dialog 1413). Layout ours: the expanders as group boxes; the Operation expander's advanced part (Overflow) always shown (its
   open state is NOT DECODED); the list's buttons labelled by their tooltip titles. Error state as the Extrude panel: a red border, the
   validator's text as the tooltip; the OK buttons are disabled while an error is shown (FunctionPanel HasError). */
static const char *ERR_CSS = "border: 1.5px solid rgb(200,30,30);";
static void setErr(QWidget *w, const char *err, QLabel *lab = nullptr) {
    w->setStyleSheet(err ? QString("%1 { %2 }").arg(w->metaObject()->className(), ERR_CSS) : QString());
    w->setToolTip(err ? QString::fromUtf8(err) : QString());
    if (lab) { lab->setStyleSheet(err ? "color: rgb(200,30,30);" : ""); lab->setToolTip(w->toolTip()); }
}

FilletPanel::FilletPanel(QWidget *parent) : QWidget(parent) {
    auto *v = new QVBoxLayout(this); v->setContentsMargins(4, 4, 4, 4); v->setSpacing(4);
    mode_ = new QStackedWidget; v->addWidget(mode_, 1);

    /* ---- the Solid Selection dialog (1413): Edge 2953, Face 2954, Body 2960 (toggles), UnSelect All 2961, OK 1 */
    auto *sel = new QWidget; auto *sv = new QVBoxLayout(sel); sv->setContentsMargins(0, 0, 0, 0);
    auto *sg = new QGroupBox("Solid Selection"); auto *sgl = new QVBoxLayout(sg);
    auto *r1 = new QHBoxLayout; bEdge_ = new QPushButton("Edge"); bFace_ = new QPushButton("Face"); r1->addWidget(bEdge_); r1->addWidget(bFace_); sgl->addLayout(r1);
    auto *r2 = new QHBoxLayout; bBody_ = new QPushButton("Body"); r2->addWidget(bBody_); r2->addStretch(1); sgl->addLayout(r2);
    for (auto *b : { bEdge_, bFace_, bBody_ }) b->setCheckable(true);
    unselAll_ = new QPushButton("UnSelect All"); sgl->addWidget(unselAll_);
    selCount_ = new QLabel; sgl->addWidget(selCount_);
    selOk_ = new QPushButton("OK"); sgl->addWidget(selOk_);
    sv->addWidget(sg); sv->addStretch(1);
    mode_->addWidget(sel);
    connect(bEdge_, &QPushButton::clicked, this, [this](bool on) { fl_selbtn[2] = on; edited(); });
    connect(bFace_, &QPushButton::clicked, this, [this](bool on) { fl_selbtn[1] = on; edited(); });
    connect(bBody_, &QPushButton::clicked, this, [this](bool on) { fl_selbtn[0] = on; edited(); });
    connect(unselAll_, &QPushButton::clicked, this, [this] { fl_unselect_all(); edited(); });
    connect(selOk_, &QPushButton::clicked, this, [this] { emit button(3); });

    /* ---- the panel */
    auto *pan = new QWidget; auto *pv = new QVBoxLayout(pan); pv->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabBar; tabs_->addTab("Basic"); tabs_->addTab("Advanced"); tabs_->setExpanding(false); pv->addWidget(tabs_);
    pages_ = new QStackedWidget; pv->addWidget(pages_, 1);
    connect(tabs_, &QTabBar::currentChanged, this, [this](int i) { if (filling_) return; fl_tab = i; refresh(); });
    auto *basic = new QWidget; auto *bv = new QVBoxLayout(basic); bv->setContentsMargins(0, 0, 0, 0);
    auto *op = new QGroupBox("Operation"); auto *ol = new QVBoxLayout(op);
    { auto *h = new QHBoxLayout; nameLab_ = new QLabel("Name:"); h->addWidget(nameLab_); name_ = new QLineEdit; name_->setMaxLength(63); h->addWidget(name_, 1); ol->addLayout(h); }
    prop_ = new QCheckBox("Propagate along tangencies"); mitre_ = new QCheckBox("Mitered corners"); ol->addWidget(prop_); ol->addWidget(mitre_);
    { auto *h = new QHBoxLayout; h->addWidget(new QLabel("Overflow:")); overflow_ = new QComboBox; overflow_->addItems({ "Default", "Maintain blend", "Maintain edges" });
      h->addWidget(overflow_, 1); ol->addLayout(h); }
    bv->addWidget(op);
    connect(name_, &QLineEdit::textEdited, this, [this](const QString &t) { snprintf(fl_name, sizeof fl_name, "%s", t.toUtf8().constData()); edited(); });
    connect(prop_, &QCheckBox::clicked, this, [this](bool on) { fl_set_check(0, on); edited(); });
    connect(mitre_, &QCheckBox::clicked, this, [this](bool on) { fl_set_check(1, on); edited(); });
    connect(overflow_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { fl_set_overflow(i); edited(); });
    auto *sl = new QGroupBox("Selection"); auto *sll = new QVBoxLayout(sl);
    list_ = new QListWidget; list_->setFixedHeight(110); list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setContextMenuPolicy(Qt::CustomContextMenu); sll->addWidget(list_);
    { auto *h = new QHBoxLayout; h->addStretch(1); addSel_ = new QPushButton("Add Selection"); reselAll_ = new QPushButton("Reselect All");
      h->addWidget(addSel_); h->addWidget(reselAll_); sll->addLayout(h); }
    bv->addWidget(sl);
    connect(list_, &QListWidget::itemSelectionChanged, this, [this] { if (filling_) return;
        for (int i = 0; i < list_->count(); i++) fl_list_select(i, list_->item(i)->isSelected()); edited(); });
    connect(list_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        QMenu m(this);
        m.addAction("Remove", this, [this] { fl_remove_sel(); edited(); })->setEnabled(!list_->selectedItems().isEmpty());
        m.addAction("Add", this, [this] { fl_add_sel(0); edited(); });
        m.addAction("Reselect all", this, [this] { fl_add_sel(1); edited(); });
        m.exec(list_->mapToGlobal(p)); });
    connect(addSel_, &QPushButton::clicked, this, [this] { fl_add_sel(0); edited(); });
    connect(reselAll_, &QPushButton::clicked, this, [this] { fl_add_sel(1); edited(); });
    auto *rg = new QGroupBox("Radius"); auto *rl = new QHBoxLayout(rg);
    radLab_ = new QLabel("Radius"); radius_ = new QLineEdit; rl->addWidget(radLab_); rl->addWidget(radius_, 1);
    bv->addWidget(rg); bv->addStretch(1);
    connect(radius_, &QLineEdit::textEdited, this, [this](const QString &t) { bool ok; double r = t.toDouble(&ok); if (ok) fl_set_radius(r); else fl_set_radius(0); edited(); });   /* UpdateSourceTrigger=PropertyChanged */
    pages_->addWidget(basic);
    auto *adv = new QWidget; auto *av = new QVBoxLayout(adv); av->setContentsMargins(0, 0, 0, 0);
    auto *pg = new QGroupBox("Preview"); auto *pgl = new QVBoxLayout(pg);
    autoPrev_ = new QCheckBox("Automatically preview results"); prevBtn_ = new QPushButton("Preview"); prevBtn_->setMinimumWidth(72);
    pgl->addWidget(autoPrev_); { auto *h = new QHBoxLayout; h->addWidget(prevBtn_); h->addStretch(1); pgl->addLayout(h); }
    av->addWidget(pg); av->addStretch(1); pages_->addWidget(adv);
    connect(autoPrev_, &QCheckBox::clicked, this, [this](bool on) { fl_set_check(2, on); edited(); });
    connect(prevBtn_, &QPushButton::clicked, this, [this] { fl_prev_req = 1; edited(); });
    warn_ = new QLabel; warn_->setWordWrap(true); warn_->setStyleSheet("color: rgb(160,90,0);"); pv->addWidget(warn_);
    { auto *h = new QHBoxLayout; ok_ = new QPushButton("OK"); okNew_ = new QPushButton("OK-New"); cancel_ = new QPushButton("Cancel");
      h->addStretch(1); h->addWidget(ok_); h->addWidget(okNew_); h->addWidget(cancel_); pv->addLayout(h); }
    connect(ok_, &QPushButton::clicked, this, [this] { emit button(0); });
    connect(okNew_, &QPushButton::clicked, this, [this] { emit button(1); });
    connect(cancel_, &QPushButton::clicked, this, [this] { emit button(2); });
    mode_->addWidget(pan);
}

void FilletPanel::edited() { refresh(); emit changed(); }

void FilletPanel::refresh() {
    filling_ = true;
    mode_->setCurrentIndex(fl_phase == 1 ? 1 : 0);
    bEdge_->setChecked(fl_selbtn[2]); bFace_->setChecked(fl_selbtn[1]); bBody_->setChecked(fl_selbtn[0]);
    selCount_->setText(QString::asprintf("Bodies %d, faces %d, edges %d", fl_npk[0], fl_npk[1], fl_npk[2]));
    tabs_->setCurrentIndex(fl_tab); pages_->setCurrentIndex(fl_tab);
    if (name_->text() != QString::fromUtf8(fl_name)) name_->setText(QString::fromUtf8(fl_name));
    prop_->setChecked(fl_p.propagate); mitre_->setChecked(fl_p.mitered); overflow_->setCurrentIndex(fl_p.overflow);
    if (!radius_->hasFocus()) radius_->setText(QString::asprintf("%.4f", fl_p.radius));
    autoPrev_->setChecked(fl_prev); prevBtn_->setEnabled(!fl_prev);       /* IsEnabled = !Preview (BooleanNegationConverter) */
    list_->clear();
    for (int i = 0; i < fl_list_n(); i++) { char b[32]; int t, k; fl_list_item(i, b, sizeof b, &t, &k);
        auto *it = new QListWidgetItem(QString::fromUtf8(b)); list_->addItem(it); it->setSelected(fl_list_selected(i) != 0); }
    const char *e[FLV_N]; fl_validate(e);
    setErr(name_, e[FLV_NAME], nameLab_); setErr(list_, e[FLV_LIST]); setErr(radius_, e[FLV_RADIUS], radLab_);
    bool err = e[FLV_NAME] || e[FLV_LIST] || e[FLV_RADIUS];
    ok_->setEnabled(!err); okNew_->setEnabled(!err);
    okNew_->setText(fl_edit_idx >= 0 ? "OK-Regen" : "OK-New");           /* HasOKNewButton = !ModifyParams, HasOKRegenButton = ModifyParams */
    warn_->setText(QString::fromUtf8(fl_warn)); warn_->setVisible(fl_warn[0] != 0);
    filling_ = false;
}
